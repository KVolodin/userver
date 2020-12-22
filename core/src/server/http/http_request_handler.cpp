#include "http_request_handler.hpp"

#include <stdexcept>

#include <engine/async.hpp>
#include <http/common_headers.hpp>
#include <logging/logger.hpp>
#include <server/handlers/http_handler_base_statistics.hpp>
#include <server/http/http_request.hpp>
#include <server/http/http_response.hpp>
#include "http_request_impl.hpp"

namespace server::http {
namespace {

engine::TaskWithResult<void> StartFailsafeTask(
    std::shared_ptr<request::RequestBase> request) {
  auto& http_request = dynamic_cast<http::HttpRequestImpl&>(*request);
  auto* handler = http_request.GetHttpHandler();
  static handlers::HttpHandlerStatistics dummy_statistics;

  http_request.SetHttpHandlerStatistics(dummy_statistics);

  return engine::impl::Async([request = std::move(request), handler]() {
    request->SetTaskStartTime();
    if (handler) handler->ReportMalformedRequest(*request);
    request->SetResponseNotifyTime();
    request->GetResponse().SetReady();
  });
}

}  // namespace

HttpRequestHandler::HttpRequestHandler(
    const components::ComponentContext& component_context,
    const std::optional<std::string>& logger_access_component,
    const std::optional<std::string>& logger_access_tskv_component,
    bool is_monitor, std::string server_name)
    : add_handler_disabled_(false),
      is_monitor_(is_monitor),
      server_name_(std::move(server_name)),
      rate_limit_(1, std::chrono::seconds(0)) {
  auto& logging_component =
      component_context.FindComponent<components::Logging>();

  if (logger_access_component && !logger_access_component->empty()) {
    logger_access_ = logging_component.GetLogger(*logger_access_component);
  } else {
    LOG_INFO() << "Access log is disabled";
  }

  if (logger_access_tskv_component && !logger_access_tskv_component->empty()) {
    logger_access_tskv_ =
        logging_component.GetLogger(*logger_access_tskv_component);
  } else {
    LOG_INFO() << "Access_tskv log is disabled";
  }
}

engine::TaskWithResult<void> HttpRequestHandler::StartRequestTask(
    std::shared_ptr<request::RequestBase> request) const {
  const auto& http_request =
      dynamic_cast<const http::HttpRequestImpl&>(*request);
  http_request.GetHttpResponse().SetHeader(::http::headers::kServer,
                                           server_name_);
  LOG_TRACE() << "ready=" << http_request.GetResponse().IsReady();
  if (http_request.GetResponse().IsReady()) {
    // Request is broken somehow, user handler must not be called
    request->SetTaskCreateTime();
    return StartFailsafeTask(std::move(request));
  }

  if (new_request_hook_) new_request_hook_(request);

  request->SetTaskCreateTime();

  auto* task_processor = http_request.GetTaskProcessor();
  auto* handler = http_request.GetHttpHandler();
  if (!task_processor || !handler) {
    // No handler found, response status is already set
    // by HttpRequestConstructor::CheckStatus
    return StartFailsafeTask(std::move(request));
  }
  auto throttling_enabled = handler->GetConfig().throttling_enabled;

  if (throttling_enabled && http_request.GetResponse().IsLimitReached()) {
    http_request.SetResponseStatus(HttpStatus::kTooManyRequests);
    http_request.GetHttpResponse().SetReady();
    request->SetTaskCreateTime();
    LOG_LIMITED_ERROR() << "Request throttled (too many pending responses, "
                           "limit via 'server.max_response_size_in_flight')";
    return StartFailsafeTask(std::move(request));
  }

  if (throttling_enabled && !rate_limit_.Obtain()) {
    http_request.SetResponseStatus(HttpStatus::kTooManyRequests);
    http_request.GetHttpResponse().SetReady();
    LOG_LIMITED_ERROR()
        << "Request throttled (congestion control, "
           "limit via USERVER_RPS_CCONTROL and USERVER_RPS_CCONTROL_ENABLED), "
        << "limit=" << rate_limit_.GetRatePs() << "/sec, "
        << "url=" << http_request.GetUrl();
    return StartFailsafeTask(std::move(request));
  }

  auto payload = [request = std::move(request), handler] {
    request->SetTaskStartTime();

    request::RequestContext context;
    handler->HandleRequest(*request, context);

    request->SetResponseNotifyTime();
    request->GetResponse().SetReady();
  };

  if (!is_monitor_ && throttling_enabled) {
    return engine::impl::Async(*task_processor, std::move(payload));
  } else {
    return engine::impl::CriticalAsync(*task_processor, std::move(payload));
  }
}  // namespace http

void HttpRequestHandler::DisableAddHandler() { add_handler_disabled_ = true; }

void HttpRequestHandler::AddHandler(const handlers::HttpHandlerBase& handler,
                                    engine::TaskProcessor& task_processor) {
  if (add_handler_disabled_) {
    throw std::runtime_error("handler adding disabled");
  }
  if (is_monitor_ != handler.IsMonitor()) {
    throw std::runtime_error(
        std::string("adding ") + (handler.IsMonitor() ? "" : "non-") +
        "monitor handler to " + (is_monitor_ ? "" : "non-") +
        "monitor HttpRequestHandler");
  }
  std::lock_guard<engine::Mutex> lock(handler_infos_mutex_);
  handler_info_index_.AddHandler(handler, task_processor);
}

const HandlerInfoIndex& HttpRequestHandler::GetHandlerInfoIndex() const {
  if (!add_handler_disabled_) {
    throw std::runtime_error(
        "handler adding must be disabled before GetHandlerInfoIndex() call");
  }
  return handler_info_index_;
}

void HttpRequestHandler::SetNewRequestHook(NewRequestHook hook) {
  new_request_hook_ = std::move(hook);
}

void HttpRequestHandler::SetRpsRatelimit(std::optional<size_t> rps) {
  if (rps) {
    const auto rps_val = *rps;
    if (rps_val > 0) {
      rate_limit_.SetMaxSize(rps_val);
      rate_limit_.SetUpdateInterval(
          utils::TokenBucket::Duration{std::chrono::seconds(1)} / rps_val);
    } else {
      rate_limit_.SetMaxSize(1);
      rate_limit_.SetUpdateInterval(utils::TokenBucket::Duration::max());

      rate_limit_.Drain();
    }
  } else {
    rate_limit_.SetUpdateInterval(utils::TokenBucket::Duration(0));
  }
}

}  // namespace server::http
