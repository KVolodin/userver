#pragma once

#include <cstdint>

USERVER_NAMESPACE_BEGIN

namespace kafka {

/// @brief Represents the range of offsets.
struct OffsetRange final {
    ///@brief The low watermark offset. It indicates the earliest available offset in Kafka.
    std::uint32_t low{};

    ///@brief The high watermark offset. It indicates the next offset that will be written in Kafka.
    std::uint32_t high{};
};

}  // namespace kafka

USERVER_NAMESPACE_END
