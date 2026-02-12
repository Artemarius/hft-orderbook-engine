#pragma once

/// @file event_buffer.h
/// @brief Type alias for the outbound event ring buffer.
///
/// EventBuffer is the SPSC channel between the matching engine thread
/// (producer) and the market data publisher thread (consumer).
/// 65536 slots * 64 bytes = 4 MB — sized to absorb bursts without
/// backpressure under normal conditions.

#include "transport/message.h"
#include "transport/spsc_ring_buffer.h"

namespace hft {

using EventBuffer = SPSCRingBuffer<EventMessage, 65536>;

}  // namespace hft
