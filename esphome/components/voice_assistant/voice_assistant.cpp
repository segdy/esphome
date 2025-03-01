#include "voice_assistant.h"

#include "esphome/core/log.h"

namespace esphome {
namespace voice_assistant {

static const char *const TAG = "voice_assistant";

float VoiceAssistant::get_setup_priority() const { return setup_priority::AFTER_CONNECTION; }

void VoiceAssistant::setup() {
  ESP_LOGCONFIG(TAG, "Setting up Voice Assistant...");

  global_voice_assistant = this;

  this->socket_ = socket::socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
  if (socket_ == nullptr) {
    ESP_LOGW(TAG, "Could not create socket.");
    this->mark_failed();
    return;
  }
  int enable = 1;
  int err = socket_->setsockopt(SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int));
  if (err != 0) {
    ESP_LOGW(TAG, "Socket unable to set reuseaddr: errno %d", err);
    // we can still continue
  }
  err = socket_->setblocking(false);
  if (err != 0) {
    ESP_LOGW(TAG, "Socket unable to set nonblocking mode: errno %d", err);
    this->mark_failed();
    return;
  }

  this->mic_->add_data_callback([this](const std::vector<uint8_t> &data) {
    if (!this->running_) {
      return;
    }
    this->socket_->sendto(data.data(), data.size(), 0, (struct sockaddr *) &this->dest_addr_, sizeof(this->dest_addr_));
  });
}

void VoiceAssistant::start(struct sockaddr_storage *addr, uint16_t port) {
  ESP_LOGD(TAG, "Starting...");

  memcpy(&this->dest_addr_, addr, sizeof(this->dest_addr_));
  if (this->dest_addr_.ss_family == AF_INET) {
    ((struct sockaddr_in *) &this->dest_addr_)->sin_port = htons(port);
  }
#if LWIP_IPV6
  else if (this->dest_addr_.ss_family == AF_INET6) {
    ((struct sockaddr_in6 *) &this->dest_addr_)->sin6_port = htons(port);
  }
#endif
  else {
    ESP_LOGW(TAG, "Unknown address family: %d", this->dest_addr_.ss_family);
    return;
  }
  this->running_ = true;
  this->mic_->start();
}

void VoiceAssistant::request_start() {
  ESP_LOGD(TAG, "Requesting start...");
  if (!api::global_api_server->start_voice_assistant()) {
    ESP_LOGW(TAG, "Could not request start.");
    this->error_trigger_->trigger("not-connected", "Could not request start.");
  }
}

void VoiceAssistant::signal_stop() {
  ESP_LOGD(TAG, "Signaling stop...");
  this->mic_->stop();
  this->running_ = false;
  api::global_api_server->stop_voice_assistant();
  memset(&this->dest_addr_, 0, sizeof(this->dest_addr_));
}

void VoiceAssistant::on_event(const api::VoiceAssistantEventResponse &msg) {
  switch (msg.event_type) {
    case api::enums::VOICE_ASSISTANT_RUN_START:
      ESP_LOGD(TAG, "Assist Pipeline running");
      this->start_trigger_->trigger();
      break;
    case api::enums::VOICE_ASSISTANT_STT_END: {
      std::string text;
      for (auto arg : msg.data) {
        if (arg.name == "text") {
          text = std::move(arg.value);
        }
      }
      if (text.empty()) {
        ESP_LOGW(TAG, "No text in STT_END event.");
        return;
      }
      ESP_LOGD(TAG, "Speech recognised as: \"%s\"", text.c_str());
      this->stt_end_trigger_->trigger(text);
      break;
    }
    case api::enums::VOICE_ASSISTANT_TTS_START: {
      std::string text;
      for (auto arg : msg.data) {
        if (arg.name == "text") {
          text = std::move(arg.value);
        }
      }
      if (text.empty()) {
        ESP_LOGW(TAG, "No text in TTS_START event.");
        return;
      }
      ESP_LOGD(TAG, "Response: \"%s\"", text.c_str());
      this->tts_start_trigger_->trigger(text);
      break;
    }
    case api::enums::VOICE_ASSISTANT_TTS_END: {
      std::string url;
      for (auto arg : msg.data) {
        if (arg.name == "url") {
          url = std::move(arg.value);
        }
      }
      if (url.empty()) {
        ESP_LOGW(TAG, "No url in TTS_END event.");
        return;
      }
      ESP_LOGD(TAG, "Response URL: \"%s\"", url.c_str());
      this->tts_end_trigger_->trigger(url);
      break;
    }
    case api::enums::VOICE_ASSISTANT_RUN_END:
      ESP_LOGD(TAG, "Assist Pipeline ended");
      this->end_trigger_->trigger();
      break;
    case api::enums::VOICE_ASSISTANT_ERROR: {
      std::string code = "";
      std::string message = "";
      for (auto arg : msg.data) {
        if (arg.name == "code") {
          code = std::move(arg.value);
        } else if (arg.name == "message") {
          message = std::move(arg.value);
        }
      }
      ESP_LOGE(TAG, "Error: %s - %s", code.c_str(), message.c_str());
      this->error_trigger_->trigger(code, message);
    }
    default:
      break;
  }
}

VoiceAssistant *global_voice_assistant = nullptr;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

}  // namespace voice_assistant
}  // namespace esphome
