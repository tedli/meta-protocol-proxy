#include "src/meta_protocol_proxy/filters/local_ratelimit/local_ratelimit.h"
#include "src/meta_protocol_proxy/app_exception.h"
#include "common/buffer/buffer_impl.h"
#include "envoy/extensions/filters/network/local_ratelimit/v3/local_rate_limit.pb.h"
#include "src/meta_protocol_proxy/filters/local_ratelimit/local_ratelimit_impl.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MetaProtocolProxy {
namespace LocalRateLimit {

FilterConfig::FilterConfig(const LocalRateLimitConfig& cfg, Stats::Scope& scope,
                           Event::Dispatcher& dispatcher)
    : stats_(generateStats(cfg.stat_prefix(), scope)),
      rate_limiter_(LocalRateLimiterImpl(
          std::chrono::milliseconds(
              PROTOBUF_GET_MS_OR_DEFAULT(cfg.token_bucket(), fill_interval, 0)),
          cfg.token_bucket().max_tokens(),
          PROTOBUF_GET_WRAPPED_OR_DEFAULT(cfg.token_bucket(), tokens_per_fill, 1), dispatcher,
          cfg.conditions(), cfg)),
      config_(cfg) {}

LocalRateLimitStats FilterConfig::generateStats(const std::string& prefix, Stats::Scope& scope) {
  const std::string final_prefix = prefix + ".local_rate_limit";
  return {ALL_LOCAL_RATE_LIMIT_STATS(POOL_COUNTER_PREFIX(scope, final_prefix))};
}

void LocalRateLimit::onDestroy() { cleanup(); }

void LocalRateLimit::setDecoderFilterCallbacks(DecoderFilterCallbacks& callbacks) {
  callbacks_ = &callbacks;
}

FilterStatus LocalRateLimit::onMessageDecoded(MetadataSharedPtr metadata, MutationSharedPtr) {

  if (shouldRateLimit(metadata)) {
    ENVOY_STREAM_LOG(debug, "meta protocol local rate limit:  '{}'", *callbacks_,
                     metadata->getRequestId());
    callbacks_->sendLocalReply(
        AppException(
            Error{ErrorType::OverLimit,
                  fmt::format("meta protocol local rate limit: request '{}' has been rate limited",
                              metadata->getRequestId())}),
        false);
    return FilterStatus::StopIteration;
  }

  ENVOY_STREAM_LOG(debug, "meta protocol local rate limit: onMessageDecoded", *callbacks_);
  return FilterStatus::Continue;
}

void LocalRateLimit::setEncoderFilterCallbacks(EncoderFilterCallbacks& callbacks) {
  encoder_callbacks_ = &callbacks;
}

FilterStatus LocalRateLimit::onMessageEncoded(MetadataSharedPtr, MutationSharedPtr) {
  return FilterStatus::Continue;
}

void LocalRateLimit::cleanup() {}

bool LocalRateLimit::shouldRateLimit(MetadataSharedPtr metadata) {
  if (filter_config_->rate_limiter_.requestAllowed(metadata)) {
    filter_config_->stats_.ok_.inc();
    return false;
  }
  filter_config_->stats_.rate_limited_.inc();
  return true;
};

} // namespace LocalRateLimit
} // namespace MetaProtocolProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy

