#pragma once

USERVER_NAMESPACE_BEGIN

namespace kafka::impl {

struct OffsetRange final {
    std::int64_t high{};
    std::int64_t low{};
};

}  // namespace kafka::impl

USERVER_NAMESPACE_END
