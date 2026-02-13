#include "feed/fix_parser.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

#include "feed/l3_feed_parser.h"

namespace hft {
namespace fix {

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

/// SOH delimiter used in real FIX messages.
static constexpr char SOH = '\x01';

/// Detect which delimiter the message uses (SOH or pipe).
static char detect_delimiter(std::string_view raw) {
    // If the message contains SOH, use that; otherwise assume pipe.
    for (char c : raw) {
        if (c == SOH) return SOH;
    }
    return '|';
}

/// Parse an integer from a string_view. Returns -1 on failure.
static int parse_int(std::string_view sv) {
    if (sv.empty()) return -1;
    int result = 0;
    for (char c : sv) {
        if (c < '0' || c > '9') return -1;
        result = result * 10 + (c - '0');
    }
    return result;
}

// ---------------------------------------------------------------------------
// FixParser — public API
// ---------------------------------------------------------------------------

FixMessage FixParser::parse(std::string_view raw) {
    FixMessage msg;

    if (raw.empty()) {
        msg.error = "empty message";
        return msg;
    }

    char delim = detect_delimiter(raw);

    // Tag lookup: flat array for tags 0-200, small array for tags 150-199
    // We use string_view into the original raw buffer for zero-copy.
    // Tags 0-60 cover most fields; 150-151 are ExecType/LeavesQty.
    constexpr size_t PRIMARY_SIZE = 201;
    std::array<std::string_view, PRIMARY_SIZE> tags{};

    // Parse all tag=value pairs
    size_t pos = 0;
    while (pos < raw.size()) {
        // Find next delimiter or end
        size_t end = raw.find(delim, pos);
        if (end == std::string_view::npos) {
            end = raw.size();
        }

        std::string_view field = raw.substr(pos, end - pos);
        pos = end + 1;

        if (field.empty()) continue;

        // Find '='
        size_t eq = field.find('=');
        if (eq == std::string_view::npos || eq == 0) continue;

        int tag_num = parse_int(field.substr(0, eq));
        if (tag_num < 0) continue;

        std::string_view value = field.substr(eq + 1);

        if (tag_num < static_cast<int>(PRIMARY_SIZE)) {
            tags[static_cast<size_t>(tag_num)] = value;
        }
        // Tags >= 201 are not used by our subset
    }

    // --- Extract fields ---

    // Tag 8: BeginString
    if (!tags[Tag::BeginString].empty()) {
        msg.begin_string = std::string(tags[Tag::BeginString]);
    }

    // Tag 9: BodyLength
    if (!tags[Tag::BodyLength].empty()) {
        msg.body_length = parse_int(tags[Tag::BodyLength]);
    }

    // Tag 10: CheckSum
    if (!tags[Tag::CheckSum].empty()) {
        msg.checksum = std::string(tags[Tag::CheckSum]);
    }

    // Tag 35: MsgType (required)
    if (tags[Tag::MsgType].empty()) {
        msg.error = "missing MsgType (tag 35)";
        return msg;
    }
    if (tags[Tag::MsgType].size() != 1) {
        msg.error = "invalid MsgType: " + std::string(tags[Tag::MsgType]);
        return msg;
    }
    msg.msg_type = tags[Tag::MsgType][0];

    // Validate message type
    if (msg.msg_type != MsgType::NewOrderSingle &&
        msg.msg_type != MsgType::OrderCancelRequest &&
        msg.msg_type != MsgType::OrderCancelReplace &&
        msg.msg_type != MsgType::ExecutionReport) {
        msg.error = "unsupported MsgType: " + std::string(1, msg.msg_type);
        return msg;
    }

    // Tag 49: SenderCompID
    if (!tags[Tag::SenderCompID].empty()) {
        msg.sender_comp_id = std::string(tags[Tag::SenderCompID]);
    }

    // Tag 56: TargetCompID
    if (!tags[Tag::TargetCompID].empty()) {
        msg.target_comp_id = std::string(tags[Tag::TargetCompID]);
    }

    // Tag 11: ClOrdID
    if (!tags[Tag::ClOrdID].empty()) {
        msg.cl_ord_id = std::string(tags[Tag::ClOrdID]);
    }

    // Tag 41: OrigClOrdID (cancel/replace)
    if (!tags[Tag::OrigClOrdID].empty()) {
        msg.orig_cl_ord_id = std::string(tags[Tag::OrigClOrdID]);
    }

    // Tag 55: Symbol
    if (!tags[Tag::Symbol].empty()) {
        msg.symbol = std::string(tags[Tag::Symbol]);
    }

    // Tag 54: Side
    if (!tags[Tag::Side].empty() && tags[Tag::Side].size() == 1) {
        msg.fix_side = tags[Tag::Side][0];
    }

    // Tag 40: OrdType
    if (!tags[Tag::OrdType].empty() && tags[Tag::OrdType].size() == 1) {
        msg.fix_ord_type = tags[Tag::OrdType][0];
    }

    // Tag 59: TimeInForce
    if (!tags[Tag::TimeInForce].empty() && tags[Tag::TimeInForce].size() == 1) {
        msg.fix_tif = tags[Tag::TimeInForce][0];
    }

    // Tag 44: Price — parse to fixed-point via L3FeedParser
    if (!tags[Tag::Price].empty()) {
        msg.price = L3FeedParser::parse_price(tags[Tag::Price]);
    }

    // Tag 38: OrderQty
    if (!tags[Tag::OrderQty].empty()) {
        msg.quantity = L3FeedParser::parse_quantity(tags[Tag::OrderQty]);
    }

    // Tag 60: TransactTime
    if (!tags[Tag::TransactTime].empty()) {
        msg.transact_time = std::string(tags[Tag::TransactTime]);
    }

    // --- Validate required fields per message type ---

    switch (msg.msg_type) {
        case MsgType::NewOrderSingle:
            if (msg.cl_ord_id.empty()) {
                msg.error = "NewOrderSingle missing ClOrdID (tag 11)";
                return msg;
            }
            if (msg.fix_side == '\0') {
                msg.error = "NewOrderSingle missing Side (tag 54)";
                return msg;
            }
            if (msg.fix_ord_type == '\0') {
                msg.error = "NewOrderSingle missing OrdType (tag 40)";
                return msg;
            }
            if (msg.quantity == 0) {
                msg.error = "NewOrderSingle missing or zero OrderQty (tag 38)";
                return msg;
            }
            if (msg.symbol.empty()) {
                msg.error = "NewOrderSingle missing Symbol (tag 55)";
                return msg;
            }
            // Limit orders require price
            if (msg.fix_ord_type == OrdTypeValue::Limit && msg.price == 0) {
                msg.error = "Limit order missing Price (tag 44)";
                return msg;
            }
            break;

        case MsgType::OrderCancelRequest:
            if (msg.cl_ord_id.empty()) {
                msg.error = "OrderCancelRequest missing ClOrdID (tag 11)";
                return msg;
            }
            if (msg.orig_cl_ord_id.empty()) {
                msg.error = "OrderCancelRequest missing OrigClOrdID (tag 41)";
                return msg;
            }
            if (msg.symbol.empty()) {
                msg.error = "OrderCancelRequest missing Symbol (tag 55)";
                return msg;
            }
            break;

        case MsgType::OrderCancelReplace:
            if (msg.cl_ord_id.empty()) {
                msg.error = "OrderCancelReplace missing ClOrdID (tag 11)";
                return msg;
            }
            if (msg.orig_cl_ord_id.empty()) {
                msg.error = "OrderCancelReplace missing OrigClOrdID (tag 41)";
                return msg;
            }
            if (msg.fix_side == '\0') {
                msg.error = "OrderCancelReplace missing Side (tag 54)";
                return msg;
            }
            if (msg.symbol.empty()) {
                msg.error = "OrderCancelReplace missing Symbol (tag 55)";
                return msg;
            }
            if (msg.fix_ord_type == '\0') {
                msg.error = "OrderCancelReplace missing OrdType (tag 40)";
                return msg;
            }
            if (msg.quantity == 0) {
                msg.error = "OrderCancelReplace missing or zero OrderQty (tag 38)";
                return msg;
            }
            break;

        case MsgType::ExecutionReport:
            // Execution reports are inbound for the serializer path;
            // we just parse them without strict validation here.
            break;

        default:
            break;
    }

    // --- Checksum validation (if tag 10 present) ---
    if (!msg.checksum.empty()) {
        if (!validate_checksum(raw)) {
            msg.error = "checksum mismatch";
            return msg;
        }
    }

    // --- BodyLength validation (if tag 9 present) ---
    if (msg.body_length > 0) {
        // Body starts after "8=...<delim>9=...<delim>" and ends before "10=..."
        // Find the end of tag 9's value (start of body)
        std::string tag9_prefix = "9=";
        size_t tag9_pos = raw.find(tag9_prefix);
        if (tag9_pos != std::string_view::npos) {
            size_t body_start = raw.find(delim, tag9_pos);
            if (body_start != std::string_view::npos) {
                body_start += 1;  // skip delimiter

                // Find "10=" — body ends just before it
                std::string tag10_marker = std::string(1, delim) + "10=";
                size_t tag10_delim = raw.find(tag10_marker, body_start);
                // Also check if body starts with "10="
                std::string tag10_start = "10=";
                if (tag10_delim == std::string_view::npos) {
                    // Check if raw ends with 10=xxx (no leading delimiter)
                    size_t bare_10 = raw.find(tag10_start, body_start);
                    if (bare_10 != std::string_view::npos) {
                        tag10_delim = bare_10 - 1;  // point to char before '1' in '10='
                    }
                }

                if (tag10_delim != std::string_view::npos) {
                    // Body length = from body_start to tag10_delim (inclusive of delimiter before 10=)
                    size_t actual_body_len = tag10_delim + 1 - body_start;
                    if (static_cast<int>(actual_body_len) != msg.body_length) {
                        msg.error = "BodyLength mismatch: declared " +
                                    std::to_string(msg.body_length) + ", actual " +
                                    std::to_string(actual_body_len);
                        return msg;
                    }
                }
            }
        }
    }

    msg.valid = true;
    return msg;
}

OrderMessage FixParser::to_order_message(const FixMessage& msg,
                                          InstrumentId id) {
    OrderMessage om{};
    om.instrument_id = id;

    switch (msg.msg_type) {
        case MsgType::NewOrderSingle:
            om.type = MessageType::Add;
            break;
        case MsgType::OrderCancelRequest:
            om.type = MessageType::Cancel;
            break;
        case MsgType::OrderCancelReplace:
            om.type = MessageType::Modify;
            break;
        default:
            om.type = MessageType::Add;  // fallback
            break;
    }

    // Order ID: hash ClOrdID for new/cancel-replace, hash OrigClOrdID for cancel
    if (om.type == MessageType::Cancel && !msg.orig_cl_ord_id.empty()) {
        om.order.order_id = cl_ord_id_to_order_id(msg.orig_cl_ord_id);
    } else if (!msg.cl_ord_id.empty()) {
        om.order.order_id = cl_ord_id_to_order_id(msg.cl_ord_id);
    }

    // For modify, we need the original order ID to find the resting order
    if (om.type == MessageType::Modify && !msg.orig_cl_ord_id.empty()) {
        om.order.order_id = cl_ord_id_to_order_id(msg.orig_cl_ord_id);
    }

    om.order.participant_id = 0;
    om.order.instrument_id = id;
    om.order.side = (msg.fix_side != '\0') ? parse_side(msg.fix_side) : Side::Buy;

    // Map OrdType + TIF to engine types
    if (msg.fix_ord_type != '\0') {
        om.order.type = parse_ord_type(msg.fix_ord_type);
    } else {
        om.order.type = OrderType::Limit;
    }

    // TimeInForce: if TIF field is set, use it; also handle IOC/FOK override
    if (msg.fix_tif != '\0') {
        om.order.time_in_force = parse_tif(msg.fix_tif);
        // IOC and FOK TIF override the order type for the engine
        if (msg.fix_tif == TifValue::IOC) {
            om.order.type = OrderType::IOC;
        } else if (msg.fix_tif == TifValue::FOK) {
            om.order.type = OrderType::FOK;
        }
    } else {
        om.order.time_in_force = TimeInForce::GTC;
    }

    om.order.status = OrderStatus::New;
    om.order.price = msg.price;
    om.order.quantity = msg.quantity;
    om.order.visible_quantity = msg.quantity;
    om.order.iceberg_slice_qty = 0;
    om.order.filled_quantity = 0;
    om.order.timestamp = 0;
    om.order.next = nullptr;
    om.order.prev = nullptr;

    return om;
}

std::string FixParser::pipe_to_soh(std::string_view pipe_msg) {
    std::string result;
    result.reserve(pipe_msg.size());
    for (char c : pipe_msg) {
        result += (c == '|') ? SOH : c;
    }
    return result;
}

bool FixParser::validate_checksum(std::string_view raw) {
    char delim = detect_delimiter(raw);

    // Find "10=" preceded by delimiter (or at very start, unlikely)
    std::string tag10_with_delim = std::string(1, delim) + "10=";
    size_t tag10_pos = raw.find(tag10_with_delim);
    size_t checksum_body_end;

    if (tag10_pos != std::string_view::npos) {
        checksum_body_end = tag10_pos + 1;  // include the delimiter before 10=
    } else {
        // Tag 10 might be at the very start (degenerate case)
        return false;
    }

    // Extract declared checksum value
    size_t val_start = tag10_pos + tag10_with_delim.size();
    size_t val_end = raw.find(delim, val_start);
    if (val_end == std::string_view::npos) {
        val_end = raw.size();
    }
    std::string_view declared_str = raw.substr(val_start, val_end - val_start);
    int declared = parse_int(declared_str);
    if (declared < 0 || declared > 255) return false;

    // Compute checksum over bytes from start up to (not including) the delimiter before "10="
    uint8_t computed = compute_checksum(raw.substr(0, checksum_body_end));

    return computed == static_cast<uint8_t>(declared);
}

uint8_t FixParser::compute_checksum(std::string_view data) {
    uint32_t sum = 0;
    for (char c : data) {
        sum += static_cast<uint8_t>(c);
    }
    return static_cast<uint8_t>(sum % 256);
}

OrderId FixParser::cl_ord_id_to_order_id(std::string_view cl_ord_id) {
    // FNV-1a 64-bit hash
    constexpr uint64_t FNV_OFFSET_BASIS = 14695981039346656037ULL;
    constexpr uint64_t FNV_PRIME = 1099511628211ULL;

    uint64_t hash = FNV_OFFSET_BASIS;
    for (char c : cl_ord_id) {
        hash ^= static_cast<uint64_t>(static_cast<uint8_t>(c));
        hash *= FNV_PRIME;
    }
    return hash;
}

Side FixParser::parse_side(char c) {
    switch (c) {
        case SideValue::Buy:  return Side::Buy;
        case SideValue::Sell: return Side::Sell;
        default: return Side::Buy;  // default fallback
    }
}

OrderType FixParser::parse_ord_type(char c) {
    switch (c) {
        case OrdTypeValue::Market: return OrderType::Market;
        case OrdTypeValue::Limit:  return OrderType::Limit;
        default: return OrderType::Limit;  // default fallback
    }
}

TimeInForce FixParser::parse_tif(char c) {
    switch (c) {
        case TifValue::Day: return TimeInForce::DAY;
        case TifValue::GTC: return TimeInForce::GTC;
        case TifValue::IOC: return TimeInForce::IOC;
        case TifValue::FOK: return TimeInForce::FOK;
        default: return TimeInForce::GTC;  // default fallback
    }
}

}  // namespace fix
}  // namespace hft
