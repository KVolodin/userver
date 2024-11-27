#include <userver/kafka/consumer_scope.hpp>

#include <userver/kafka/impl/consumer.hpp>

USERVER_NAMESPACE_BEGIN

namespace kafka {

ConsumerScope::ConsumerScope(impl::Consumer& consumer) noexcept : consumer_(consumer) {}

ConsumerScope::~ConsumerScope() { Stop(); }

void ConsumerScope::Start(Callback callback) { consumer_.StartMessageProcessing(std::move(callback)); }

void ConsumerScope::Stop() noexcept { consumer_.Stop(); }

void ConsumerScope::AsyncCommit() { consumer_.AsyncCommit(); }

std::pair<std::int64_t, std::int64_t> ConsumerScope::GetMinMaxOffset(const std::string& topic, std::int32_t partition) {
    return consumer_.GetMinMaxOffset(topic, partition);
}

std::vector<std::int32_t> ConsumerScope::GetPartitionsId(const std::string& topic){
    return consumer_.GetPartitionsId(topic);
}

}  // namespace kafka

USERVER_NAMESPACE_END
