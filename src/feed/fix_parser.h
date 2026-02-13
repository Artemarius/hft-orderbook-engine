#pragma once

/// @file fix_parser.h
/// @brief FIX 4.2 inbound message parser — raw FIX string to OrderMessage.
///
/// Cold-path component. Parses FIX tag-value messages (SOH or pipe delimited)
/// into the intermediate FixMessage struct, then converts to the engine's
/// internal OrderMessage format for gateway injection.
///
/// Supported inbound message types:
///   35=D  New Order Single   -> MessageType::Add
///   35=F  Order Cancel Request -> MessageType::Cancel
///   35=G  Order Cancel/Replace -> MessageType::Modify

#include <string>
#include <string_view>

#include "core/types.h"
#include "feed/fix_message.h"
#include "transport/message.h"

namespace hft {
namespace fix {

class FixParser {
public:
    /// Parse a raw FIX string (SOH or pipe delimited) into a FixMessage.
    /// On failure, the returned FixMessage has valid=false and error set.
    static FixMessage parse(std::string_view raw);

    /// Convert a parsed FixMessage into an OrderMessage for the gateway.
    /// Requires msg.valid == true. Maps D->Add, F->Cancel, G->Modify.
    static OrderMessage to_order_message(
        const FixMessage& msg,
        InstrumentId id = DEFAULT_INSTRUMENT_ID);

    /// Convert pipe-delimited FIX to SOH-delimited.
    static std::string pipe_to_soh(std::string_view pipe_msg);

    /// Validate the FIX checksum (tag 10) against the message body.
    static bool validate_checksum(std::string_view raw);

    /// Compute the FIX checksum: sum of all bytes modulo 256.
    /// @param data  Bytes from start of message up to (not including) "10=".
    static uint8_t compute_checksum(std::string_view data);

    /// Hash a ClOrdID string to an OrderId using FNV-1a.
    /// Deterministic and reasonably collision-resistant for demo data.
    /// NOTE: Not suitable for production use — collisions are possible.
    static OrderId cl_ord_id_to_order_id(std::string_view cl_ord_id);

    /// Map FIX Side char ('1'/'2') to engine Side enum.
    static Side parse_side(char c);

    /// Map FIX OrdType char ('1'/'2') to engine OrderType enum.
    static OrderType parse_ord_type(char c);

    /// Map FIX TimeInForce char ('0'/'1'/'3'/'4') to engine TimeInForce enum.
    static TimeInForce parse_tif(char c);
};

}  // namespace fix
}  // namespace hft
