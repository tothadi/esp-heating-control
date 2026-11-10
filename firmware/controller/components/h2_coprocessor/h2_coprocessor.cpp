#include "h2_coprocessor.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "esphome/core/log.h"

namespace esphome::h2_coprocessor {

static const char *const TAG = "h2_coprocessor";

static constexpr size_t MAX_LINE_LEN = 192;            // same limit as the H2 side (cp_link.h)
static constexpr uint32_t PING_INTERVAL_MS = 60000;  // keep-alive; the link is "offline" after 3 missed answers
static constexpr uint32_t LIST_INTERVAL_MS = 300000; // refresh the device list (last-seen ages)
static constexpr size_t TEXT_MAX = 250;             // Home Assistant limits states to 255 characters

static uint8_t checksum(const std::string &s) {
  uint8_t cs = 0;
  for (char c : s)
    cs ^= static_cast<uint8_t>(c);
  return cs;
}

// --- CoprocessorSensor -------------------------------------------------------------------------------------------

bool CoprocessorSensor::matches(const std::string &id, uint16_t cluster, uint16_t attr) const {
  if (this->device_id_.empty() || id != this->device_id_)
    return false;
  for (const auto &m : this->matches_) {
    if (m.cluster == cluster && m.attr == attr)
      return true;
  }
  return false;
}

void CoprocessorSensor::on_reading(float value, uint32_t age_s) {
  const uint32_t age_ms = age_s * 1000;
  if (age_ms >= this->timeout_ms_)
    return;  // a stored value from a DUMP that is already too old
  this->last_ms_ = millis() - age_ms;
  this->has_value_ = true;
  this->publish_state(value);
}

void CoprocessorSensor::check_timeout(uint32_t now) {
  if (this->has_value_ && now - this->last_ms_ > this->timeout_ms_) {
    ESP_LOGW(TAG, "No reading from %s for %u s, value dropped", this->device_id_.c_str(),
             (unsigned) (this->timeout_ms_ / 1000));
    this->has_value_ = false;
    this->publish_state(NAN);
  }
}

// --- CoprocessorButton -------------------------------------------------------------------------------------------

void CoprocessorButton::press_action() {
  switch (this->action_) {
    case BUTTON_PERMIT_JOIN:
      this->parent_->permit_join(255);  // 255 = configured duration
      break;
    case BUTTON_STOP_JOIN:
      this->parent_->permit_join(0);
      break;
    case BUTTON_RESET:
      this->parent_->reset_coprocessor();
      break;
    case BUTTON_REFRESH:
      this->parent_->refresh();
      break;
  }
}

// --- H2Coprocessor -----------------------------------------------------------------------------------------------

void H2Coprocessor::setup() {
  // S3 IO12 drives the H2's EN line (10 kΩ pull-up + 1 µF on the board). Never drive it except during a reset:
  // keep it an input so the pull-up holds the H2 running. The pin is deliberately not set up as an output here.
  if (this->reset_pin_ != nullptr)
    this->reset_pin_->pin_mode(gpio::FLAG_INPUT);

  this->set_interval("ping", PING_INTERVAL_MS, [this]() {
    if (this->online_ && millis() - this->last_rx_ms_ > 3 * PING_INTERVAL_MS) {
      ESP_LOGW(TAG, "Co-processor not answering");
      this->online_ = false;
      this->publish_network_();
    }
    this->send_command("PING,ka");
  });
  this->set_interval("list", LIST_INTERVAL_MS, [this]() { this->send_command("LIST"); });
  this->set_interval("timeouts", 10000, [this]() {
    const uint32_t now = millis();
    for (auto *s : this->sensors_)
      s->check_timeout(now);
  });
  // The H2 may have booted long before the S3: ask for everything it knows.
  this->set_timeout("hello", 2000, [this]() { this->refresh(); });
}

void H2Coprocessor::dump_config() {
  ESP_LOGCONFIG(TAG, "H2 co-processor link:");
  LOG_PIN("  Reset pin: ", this->reset_pin_);
  ESP_LOGCONFIG(TAG, "  Permit join duration: %u s", this->permit_join_s_);
  ESP_LOGCONFIG(TAG, "  Mapped sensors: %u", (unsigned) this->sensors_.size());
  for (auto *s : this->sensors_)
    ESP_LOGCONFIG(TAG, "    %s <- %s", s->get_name().c_str(),
                  s->get_device_id().empty() ? "(not mapped)" : s->get_device_id().c_str());
}

void H2Coprocessor::loop() {
  uint8_t c;
  while (this->available() && this->read_byte(&c)) {
    if (c == '\n' || c == '\r') {
      if (!this->rx_overflow_ && !this->rx_line_.empty())
        this->handle_line_(this->rx_line_);
      this->rx_line_.clear();
      this->rx_overflow_ = false;
    } else if (this->rx_line_.size() < MAX_LINE_LEN) {
      this->rx_line_.push_back(static_cast<char>(c));
    } else {
      this->rx_overflow_ = true;
    }
  }
}

void H2Coprocessor::send_command(const std::string &body) {
  std::string payload = "CP," + body;
  char tail[8];
  snprintf(tail, sizeof(tail), "*%02X\r\n", checksum(payload));
  std::string line = "$" + payload + tail;
  ESP_LOGD(TAG, "TX %s", line.substr(0, line.size() - 2).c_str());
  this->write_str(line.c_str());
}

void H2Coprocessor::permit_join(uint8_t seconds) {
  if (seconds == 255)
    seconds = this->permit_join_s_;
  ESP_LOGI(TAG, "Permit join %u s", seconds);
  this->send_command("PJ," + std::to_string(seconds));
}

void H2Coprocessor::refresh() {
  this->send_command("PING,init");
  this->send_command("NET");
  this->send_command("LIST");
  this->send_command("DUMP");
}

void H2Coprocessor::reset_coprocessor() {
  if (this->reset_pin_ == nullptr) {
    ESP_LOGW(TAG, "No reset pin configured");
    return;
  }
  ESP_LOGI(TAG, "Resetting the co-processor");
  this->reset_pin_->digital_write(false);
  this->reset_pin_->pin_mode(gpio::FLAG_OUTPUT);
  this->set_timeout("reset", 100, [this]() {
    this->reset_pin_->pin_mode(gpio::FLAG_INPUT);  // release: the pull-up / RC brings EN back up
  });
}

void H2Coprocessor::handle_line_(const std::string &raw) {
  // Skip anything before the last '$' (ROM boot messages of the H2 on the same UART, noise).
  const size_t start = raw.rfind('$');
  if (start == std::string::npos || raw.compare(start, 4, "$CP,") != 0)
    return;
  const size_t star = raw.rfind('*');
  if (star == std::string::npos || star < start || raw.size() != star + 3)
    return;
  const std::string body = raw.substr(start + 1, star - start - 1);
  char *end = nullptr;
  const unsigned long rx_cs = strtoul(raw.c_str() + star + 1, &end, 16);
  if (end != raw.c_str() + raw.size() || rx_cs != checksum(body)) {
    ESP_LOGW(TAG, "Checksum error: %s", raw.c_str() + start);
    return;
  }
  ESP_LOGV(TAG, "RX %s", body.c_str());

  std::vector<std::string> f;
  size_t pos = 3;  // after "CP,"
  while (true) {
    const size_t comma = body.find(',', pos);
    f.push_back(body.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos));
    if (comma == std::string::npos)
      break;
    pos = comma + 1;
  }
  this->last_rx_ms_ = millis();
  if (!this->online_) {
    this->online_ = true;
    this->publish_network_();
  }
  this->handle_message_(f);
}

void H2Coprocessor::handle_message_(const std::vector<std::string> &f) {
  const std::string &type = f[0];

  if (type == "RD" && f.size() >= 7) {
    this->handle_reading_(f);
  } else if (type == "HELLO" && f.size() >= 4) {
    ESP_LOGI(TAG, "Co-processor started: %s firmware %s, protocol %s", f[1].c_str(), f[2].c_str(), f[3].c_str());
    this->fw_ = f[1] + " " + f[2];
    if (this->firmware_ != nullptr)
      this->firmware_->publish_state(this->fw_);
    // It has just (re)booted: readings come in as devices report; fetch network state and the device list.
    this->send_command("NET");
    this->send_command("LIST");
  } else if (type == "PONG" && f.size() >= 6) {
    const std::string fw = f[2] + " " + f[3];
    if (fw != this->fw_) {
      this->fw_ = fw;
      if (this->firmware_ != nullptr)
        this->firmware_->publish_state(fw);
    }
    if (f[4] != "1")
      ESP_LOGW(TAG, "Co-processor speaks protocol %s, this component expects 1", f[4].c_str());
  } else if (type == "NET" && f.size() >= 5) {
    this->net_state_ = f[1];
    this->net_channel_ = atoi(f[2].c_str());
    this->net_pan_ = f[3];
    this->net_permit_s_ = atoi(f[4].c_str());
    this->net_permit_at_ = millis();
    this->publish_network_();
    if (this->net_permit_s_ > 0) {
      // Publish once more when joining has closed (the H2 also sends NET then, this is a fallback).
      this->set_timeout("permit", this->net_permit_s_ * 1000 + 1000, [this]() {
        this->net_permit_s_ = 0;
        this->publish_network_();
      });
    }
  } else if (type == "DEV" && f.size() >= 7) {
    // DEV,<id>,<addr>,<flags>,<age_s>,<name>,<model>
    ESP_LOGI(TAG, "Device %s addr %s [%s] name '%s' model '%s' last seen %s s ago", f[1].c_str(), f[2].c_str(),
             f[3].c_str(), f[5].c_str(), f[6].c_str(), f[4].c_str());
    std::string label = !f[5].empty() ? f[5] : (!f[6].empty() ? f[6] : "?");
    if (!this->pending_list_.empty())
      this->pending_list_ += "; ";
    this->pending_list_ += f[1] + " " + label + " " + f[3];
    this->pending_count_++;
  } else if (type == "DEVEND") {
    std::string text = this->pending_list_.empty() ? std::string("(no devices)") : this->pending_list_;
    if (text.size() > TEXT_MAX)
      text = text.substr(0, TEXT_MAX - 3) + "...";
    if (this->device_list_ != nullptr)
      this->device_list_->publish_state(text);
    this->pending_list_.clear();
    this->pending_count_ = 0;
  } else if (type == "JOIN" && f.size() >= 3) {
    ESP_LOGI(TAG, "Device %s joined / announced (addr %s)", f[1].c_str(), f[2].c_str());
    this->set_timeout("list_after_join", 5000, [this]() { this->send_command("LIST"); });
  } else if (type == "LEAVE" && f.size() >= 2) {
    ESP_LOGI(TAG, "Device %s left the network", f[1].c_str());
    this->send_command("LIST");
  } else if (type == "ERR" && f.size() >= 4) {
    ESP_LOGW(TAG, "Co-processor error on %s: %s (%s)", f[1].c_str(), f[3].c_str(), f[2].c_str());
  } else if (type == "OK" || type == "DUMPEND") {
    ESP_LOGD(TAG, "%s %s", type.c_str(), f.size() > 1 ? f[1].c_str() : "");
  } else {
    ESP_LOGD(TAG, "Unhandled message %s", type.c_str());
  }
}

void H2Coprocessor::handle_reading_(const std::vector<std::string> &f) {
  // RD,<id>,<ep>,<cluster>,<attr>,<value>,<age_s>
  const std::string &id = f[1];
  const uint16_t cluster = static_cast<uint16_t>(strtoul(f[3].c_str(), nullptr, 16));
  const uint16_t attr = static_cast<uint16_t>(strtoul(f[4].c_str(), nullptr, 16));
  char *end = nullptr;
  const float value = strtof(f[5].c_str(), &end);
  if (end == f[5].c_str())
    return;
  const uint32_t age_s = static_cast<uint32_t>(strtoul(f[6].c_str(), nullptr, 10));

  bool used = false;
  for (auto *s : this->sensors_) {
    if (s->matches(id, cluster, attr)) {
      s->on_reading(value, age_s);
      used = true;
    }
  }
  if (used) {
    ESP_LOGD(TAG, "%s %04x/%04x = %.2f", id.c_str(), cluster, attr, value);
    return;
  }
  // Log each unmapped device once at INFO, so its id can be copied into coprocessor.yaml.
  const std::string key = id + "/" + f[3] + "/" + f[4];
  if (std::find(this->unmapped_logged_.begin(), this->unmapped_logged_.end(), key) == this->unmapped_logged_.end()) {
    if (this->unmapped_logged_.size() < 64)
      this->unmapped_logged_.push_back(key);
    ESP_LOGI(TAG, "Unmapped reading: device %s cluster %04x attr %04x = %.2f", id.c_str(), cluster, attr, value);
  } else {
    ESP_LOGD(TAG, "Unmapped reading: device %s cluster %04x attr %04x = %.2f", id.c_str(), cluster, attr, value);
  }
}

void H2Coprocessor::publish_network_() {
  if (this->network_ == nullptr)
    return;
  std::string text;
  if (!this->online_) {
    text = "Co-processor offline";
  } else if (this->net_state_.empty()) {
    text = this->fw_.empty() ? "Online" : this->fw_;
  } else {
    char buf[96];
    snprintf(buf, sizeof(buf), "%s %s, channel %d, PAN %s%s", this->fw_.empty() ? "?" : this->fw_.c_str(),
             this->net_state_.c_str(), this->net_channel_, this->net_pan_.c_str(),
             this->net_permit_s_ > 0 ? ", joining open" : "");
    text = buf;
  }
  this->network_->publish_state(text);
}

}  // namespace esphome::h2_coprocessor
