#include "feed/fix_serializer.h"

#include <cstdint>
#include <cstdio>
#include <string>

#include "feed/fix_parser.h"

namespace hft {
namespace fix {

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

static constexpr char SOH = '\x01';

/// Build the FIX message body (tags between 9 and 10).
static std::string build_body(
    const EventMessage& event,
    const std::string& symbol,
    const std::string& sender,
    const std::string& target,
    char delim) {

    std::string body;
    body.reserve(256);

    // Tag 35: MsgType = '8' (Execution Report)
    body += "35=8";
    body += delim;

    // Tag 49: SenderCompID
    body += "49=";
    body += sender;
    body += delim;

    // Tag 56: TargetCompID
    body += "56=";
    body += target;
    body += delim;

    // Determine fields from event type
    char exec_type = FixSerializer::to_fix_exec_type(event.type);
    char ord_status = FixSerializer::to_fix_ord_status(event.type);

    if (event.type == EventType::Trade) {
        const auto& trade = event.data.trade;

        // Tag 37: OrderID (use buy_order_id)
        body += "37=";
        body += std::to_string(trade.buy_order_id);
        body += delim;

        // Tag 11: ClOrdID (use sell_order_id as a proxy)
        body += "11=";
        body += std::to_string(trade.sell_order_id);
        body += delim;

        // Tag 17: ExecID
        body += "17=EXEC-";
        body += std::to_string(trade.trade_id);
        body += delim;

        // Tag 150: ExecType
        body += "150=";
        body += exec_type;
        body += delim;

        // Tag 39: OrdStatus
        body += "39=";
        body += ord_status;
        body += delim;

        // Tag 55: Symbol
        body += "55=";
        body += symbol;
        body += delim;

        // Tag 54: Side (Buy side of the trade)
        body += "54=";
        body += FixSerializer::to_fix_side(Side::Buy);
        body += delim;

        // Tag 44: Price
        body += "44=";
        body += FixSerializer::format_price(trade.price);
        body += delim;

        // Tag 32: LastQty
        body += "32=";
        body += std::to_string(trade.quantity);
        body += delim;

        // Tag 31: LastPx
        body += "31=";
        body += FixSerializer::format_price(trade.price);
        body += delim;

        // Tag 14: CumQty
        body += "14=";
        body += std::to_string(trade.quantity);
        body += delim;

        // Tag 151: LeavesQty
        body += "151=0";
        body += delim;

        // Tag 60: TransactTime
        body += "60=";
        body += std::to_string(trade.timestamp);
        body += delim;

    } else {
        // Order event (Accepted, Cancelled, Rejected, Filled, PartialFill, Modified)
        const auto& oe = event.data.order_event;

        // Tag 37: OrderID
        body += "37=";
        body += std::to_string(oe.order_id);
        body += delim;

        // Tag 11: ClOrdID (same as OrderID for simplicity)
        body += "11=";
        body += std::to_string(oe.order_id);
        body += delim;

        // Tag 17: ExecID
        body += "17=EXEC-";
        body += std::to_string(event.sequence_num);
        body += delim;

        // Tag 150: ExecType
        body += "150=";
        body += exec_type;
        body += delim;

        // Tag 39: OrdStatus
        body += "39=";
        body += ord_status;
        body += delim;

        // Tag 55: Symbol
        body += "55=";
        body += symbol;
        body += delim;

        // Tag 54: Side (not available in OrderEventData; default to Buy)
        body += "54=1";
        body += delim;

        // Tag 44: Price
        body += "44=";
        body += FixSerializer::format_price(oe.price);
        body += delim;

        // Tag 38: OrderQty (use filled + remaining as proxy for original qty)
        body += "38=";
        body += std::to_string(oe.filled_quantity + oe.remaining_quantity);
        body += delim;

        // Tag 14: CumQty
        body += "14=";
        body += std::to_string(oe.filled_quantity);
        body += delim;

        // Tag 151: LeavesQty
        body += "151=";
        body += std::to_string(oe.remaining_quantity);
        body += delim;

        // Tag 60: TransactTime
        body += "60=";
        body += std::to_string(oe.timestamp);
        body += delim;
    }

    return body;
}

/// Wrap body with BeginString, BodyLength, and CheckSum.
static std::string wrap_message(const std::string& body, char delim) {
    // Tag 8 and 9 prefix
    std::string prefix;
    prefix += "8=FIX.4.2";
    prefix += delim;
    prefix += "9=";
    prefix += std::to_string(body.size());
    prefix += delim;

    // Compute checksum over prefix + body
    std::string pre_checksum = prefix + body;
    uint8_t cs = FixParser::compute_checksum(pre_checksum);

    // Format checksum as 3-digit zero-padded
    char cs_buf[4];
    std::snprintf(cs_buf, sizeof(cs_buf), "%03u", static_cast<unsigned>(cs));

    std::string result = pre_checksum;
    result += "10=";
    result += cs_buf;
    result += delim;

    return result;
}

// ---------------------------------------------------------------------------
// FixSerializer — public API
// ---------------------------------------------------------------------------

std::string FixSerializer::to_execution_report(
    const EventMessage& event,
    const std::string& symbol,
    const std::string& sender,
    const std::string& target) {

    std::string body = build_body(event, symbol, sender, target, SOH);
    return wrap_message(body, SOH);
}

std::string FixSerializer::to_execution_report_pretty(
    const EventMessage& event,
    const std::string& symbol,
    const std::string& sender,
    const std::string& target) {

    std::string body = build_body(event, symbol, sender, target, '|');
    return wrap_message(body, '|');
}

char FixSerializer::to_fix_side(Side side) {
    switch (side) {
        case Side::Buy:  return fix::SideValue::Buy;
        case Side::Sell: return fix::SideValue::Sell;
        default: return fix::SideValue::Buy;
    }
}

char FixSerializer::to_fix_exec_type(EventType type) {
    switch (type) {
        case EventType::OrderAccepted:    return fix::ExecTypeValue::New;
        case EventType::OrderPartialFill: return fix::ExecTypeValue::PartialFill;
        case EventType::OrderFilled:
        case EventType::Trade:            return fix::ExecTypeValue::Fill;
        case EventType::OrderCancelled:   return fix::ExecTypeValue::Cancelled;
        case EventType::OrderRejected:    return fix::ExecTypeValue::Rejected;
        case EventType::OrderModified:    return fix::ExecTypeValue::Replaced;
        default: return fix::ExecTypeValue::New;
    }
}

char FixSerializer::to_fix_ord_status(EventType type) {
    switch (type) {
        case EventType::OrderAccepted:    return fix::OrdStatusValue::New;
        case EventType::OrderPartialFill: return fix::OrdStatusValue::PartialFill;
        case EventType::OrderFilled:
        case EventType::Trade:            return fix::OrdStatusValue::Filled;
        case EventType::OrderCancelled:   return fix::OrdStatusValue::Cancelled;
        case EventType::OrderRejected:    return fix::OrdStatusValue::Rejected;
        case EventType::OrderModified:    return fix::OrdStatusValue::Replaced;
        default: return fix::OrdStatusValue::New;
    }
}

std::string FixSerializer::format_price(Price price) {
    if (price == 0) return "0.00000000";

    bool negative = (price < 0);
    int64_t abs_price = negative ? -price : price;

    int64_t integer_part = abs_price / PRICE_SCALE;
    int64_t frac_part = abs_price % PRICE_SCALE;

    // Format fractional part as 8 digits with leading zeros
    std::string frac_str = std::to_string(frac_part);
    while (frac_str.size() < 8) {
        frac_str.insert(frac_str.begin(), '0');
    }

    std::string result;
    if (negative) result += "-";
    result += std::to_string(integer_part);
    result += ".";
    result += frac_str;
    return result;
}

}  // namespace fix
}  // namespace hft
