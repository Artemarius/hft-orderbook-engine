#include "feed/l3_feed_parser.h"

#include <algorithm>
#include <cctype>

namespace hft {

// ---------------------------------------------------------------------------
// File I/O
// ---------------------------------------------------------------------------

L3FeedParser::~L3FeedParser() {
    close();
}

bool L3FeedParser::open(const std::string& path) {
    close();
    file_.open(path);
    if (!file_.is_open()) {
        return false;
    }
    lines_read_ = 0;
    parse_errors_ = 0;
    has_symbol_column_ = false;
    return true;
}

bool L3FeedParser::next(L3Record& record) {
    while (std::getline(file_, current_line_)) {
        ++lines_read_;

        // Trim trailing whitespace / carriage return
        while (!current_line_.empty() &&
               (current_line_.back() == '\r' || current_line_.back() == '\n' ||
                current_line_.back() == ' ')) {
            current_line_.pop_back();
        }

        // Skip empty lines
        if (current_line_.empty()) {
            continue;
        }

        // Skip header lines
        if (is_header(current_line_)) {
            continue;
        }

        if (!parse_line(current_line_, record)) {
            ++parse_errors_;
            record.valid = false;
            // record.error is set by parse_line
            return true;  // return the invalid record so caller can log it
        }

        return true;
    }

    return false;  // EOF
}

void L3FeedParser::reset() {
    if (file_.is_open()) {
        file_.clear();
        file_.seekg(0);
        lines_read_ = 0;
        parse_errors_ = 0;
        has_symbol_column_ = false;
    }
}

void L3FeedParser::close() {
    if (file_.is_open()) {
        file_.close();
    }
}

// ---------------------------------------------------------------------------
// Static parsing utilities
// ---------------------------------------------------------------------------

std::vector<std::string_view> L3FeedParser::split_csv(std::string_view line) {
    std::vector<std::string_view> fields;
    size_t start = 0;

    while (start <= line.size()) {
        size_t comma = line.find(',', start);
        if (comma == std::string_view::npos) {
            fields.push_back(line.substr(start));
            break;
        }
        fields.push_back(line.substr(start, comma - start));
        start = comma + 1;
    }

    return fields;
}

L3EventType L3FeedParser::parse_event_type(std::string_view str) {
    // Case-insensitive comparison
    if (str.size() == 3) {
        char c0 = static_cast<char>(std::toupper(static_cast<unsigned char>(str[0])));
        char c1 = static_cast<char>(std::toupper(static_cast<unsigned char>(str[1])));
        char c2 = static_cast<char>(std::toupper(static_cast<unsigned char>(str[2])));
        if (c0 == 'A' && c1 == 'D' && c2 == 'D') return L3EventType::Add;
    }
    if (str.size() == 6) {
        std::string upper(str.size(), '\0');
        for (size_t i = 0; i < str.size(); ++i) {
            upper[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(str[i])));
        }
        if (upper == "CANCEL") return L3EventType::Cancel;
        if (upper == "MODIFY") return L3EventType::Modify;
    }
    if (str.size() == 5) {
        std::string upper(str.size(), '\0');
        for (size_t i = 0; i < str.size(); ++i) {
            upper[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(str[i])));
        }
        if (upper == "TRADE") return L3EventType::Trade;
    }
    return L3EventType::Invalid;
}

Side L3FeedParser::parse_side(std::string_view str, bool& ok) {
    ok = false;
    if (str.size() == 3) {
        char c0 = static_cast<char>(std::toupper(static_cast<unsigned char>(str[0])));
        char c1 = static_cast<char>(std::toupper(static_cast<unsigned char>(str[1])));
        char c2 = static_cast<char>(std::toupper(static_cast<unsigned char>(str[2])));
        if (c0 == 'B' && c1 == 'U' && c2 == 'Y') {
            ok = true;
            return Side::Buy;
        }
    }
    if (str.size() == 4) {
        std::string upper(str.size(), '\0');
        for (size_t i = 0; i < str.size(); ++i) {
            upper[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(str[i])));
        }
        if (upper == "SELL") {
            ok = true;
            return Side::Sell;
        }
    }
    return Side::Buy;
}

Price L3FeedParser::parse_price(std::string_view str) {
    if (str.empty()) return 0;

    // Integer-only parsing: split on '.', parse integer and fractional parts
    // separately, then combine: price = integer_part * PRICE_SCALE + frac_scaled

    bool negative = false;
    size_t pos = 0;
    if (str[0] == '-') {
        negative = true;
        pos = 1;
    } else if (str[0] == '+') {
        pos = 1;
    }

    if (pos >= str.size()) return 0;

    // Find decimal point
    size_t dot_pos = str.find('.', pos);

    // Parse integer part
    int64_t integer_part = 0;
    size_t int_end = (dot_pos != std::string_view::npos) ? dot_pos : str.size();
    for (size_t i = pos; i < int_end; ++i) {
        if (str[i] < '0' || str[i] > '9') return 0;
        integer_part = integer_part * 10 + (str[i] - '0');
    }

    // Parse fractional part
    int64_t frac_scaled = 0;
    if (dot_pos != std::string_view::npos) {
        // PRICE_SCALE = 10^8, so we need up to 8 decimal digits
        constexpr int MAX_DECIMALS = 8;
        int decimals = 0;
        for (size_t i = dot_pos + 1; i < str.size() && decimals < MAX_DECIMALS; ++i) {
            if (str[i] < '0' || str[i] > '9') return 0;
            frac_scaled = frac_scaled * 10 + (str[i] - '0');
            ++decimals;
        }
        // Pad remaining decimal places with zeros
        for (int i = decimals; i < MAX_DECIMALS; ++i) {
            frac_scaled *= 10;
        }
        // Validate any remaining digits are zeros (truncation)
        for (size_t i = dot_pos + 1 + MAX_DECIMALS; i < str.size(); ++i) {
            if (str[i] < '0' || str[i] > '9') return 0;
            // Extra digits beyond precision are ignored
        }
    }

    Price result = integer_part * PRICE_SCALE + frac_scaled;
    return negative ? -result : result;
}

Quantity L3FeedParser::parse_quantity(std::string_view str) {
    if (str.empty()) return 0;

    uint64_t result = 0;
    for (char c : str) {
        if (c < '0' || c > '9') return 0;
        result = result * 10 + static_cast<uint64_t>(c - '0');
    }
    return result;
}

OrderMessage L3FeedParser::to_order_message(const L3Record& record,
                                             InstrumentId instrument_id) {
    OrderMessage msg{};
    msg.type = MessageType::Add;
    msg.instrument_id = instrument_id;
    msg.order.order_id = record.order_id;
    msg.order.participant_id = 0;  // L3 feed doesn't carry participant ID
    msg.order.instrument_id = instrument_id;
    msg.order.side = record.side;
    msg.order.type = OrderType::Limit;
    msg.order.time_in_force = TimeInForce::GTC;
    msg.order.status = OrderStatus::New;
    msg.order.price = record.price;
    msg.order.quantity = record.quantity;
    msg.order.visible_quantity = record.quantity;
    msg.order.iceberg_slice_qty = 0;
    msg.order.filled_quantity = 0;
    msg.order.timestamp = record.timestamp;
    msg.order.next = nullptr;
    msg.order.prev = nullptr;
    return msg;
}

OrderMessage L3FeedParser::to_modify_message(const L3Record& record,
                                              InstrumentId instrument_id) {
    OrderMessage msg{};
    msg.type = MessageType::Modify;
    msg.instrument_id = instrument_id;
    msg.order.order_id = record.order_id;
    msg.order.participant_id = 0;
    msg.order.instrument_id = instrument_id;
    msg.order.side = record.side;
    msg.order.type = OrderType::Limit;
    msg.order.time_in_force = TimeInForce::GTC;
    msg.order.status = OrderStatus::New;
    msg.order.price = record.price;
    msg.order.quantity = record.quantity;
    msg.order.visible_quantity = record.quantity;
    msg.order.iceberg_slice_qty = 0;
    msg.order.filled_quantity = 0;
    msg.order.timestamp = record.timestamp;
    msg.order.next = nullptr;
    msg.order.prev = nullptr;
    return msg;
}

// ---------------------------------------------------------------------------
// Line parsing
// ---------------------------------------------------------------------------

bool L3FeedParser::parse_line(std::string_view line, L3Record& record) {
    record = {};

    auto fields = split_csv(line);
    if (fields.size() < 2) {
        record.error = "too few fields";
        return false;
    }

    // Detect 7-column format: first field is a symbol (not a digit string)
    // Once detected on first data line, has_symbol_column_ persists for the file.
    size_t offset = 0;
    if (has_symbol_column_) {
        offset = 1;
    } else if (fields.size() >= 7 && !fields[0].empty() &&
               (fields[0][0] < '0' || fields[0][0] > '9')) {
        has_symbol_column_ = true;
        offset = 1;
    }

    if (offset == 1) {
        record.symbol = std::string(fields[0]);
    }

    size_t ts_idx = offset;
    size_t et_idx = offset + 1;

    // Field ts_idx: timestamp
    if (ts_idx >= fields.size() || fields[ts_idx].empty()) {
        record.error = "empty timestamp";
        return false;
    }
    record.timestamp = 0;
    for (char c : fields[ts_idx]) {
        if (c < '0' || c > '9') {
            record.error = "invalid timestamp";
            return false;
        }
        record.timestamp = record.timestamp * 10 + static_cast<uint64_t>(c - '0');
    }

    // Field et_idx: event_type
    if (et_idx >= fields.size()) {
        record.error = "missing event_type";
        return false;
    }
    record.event_type = parse_event_type(fields[et_idx]);
    if (record.event_type == L3EventType::Invalid) {
        record.error = "invalid event type: " + std::string(fields[1]);
        return false;
    }

    // Field indices after the symbol/timestamp/event_type preamble
    size_t oid_idx  = offset + 2;  // order_id
    size_t side_idx = offset + 3;  // side
    size_t px_idx   = offset + 4;  // price
    size_t qty_idx  = offset + 5;  // quantity
    size_t min_full = offset + 6;  // minimum fields for full records
    size_t min_cancel = offset + 3; // minimum fields for cancel

    // Remaining fields depend on event type
    switch (record.event_type) {
        case L3EventType::Add: {
            // Need: order_id, side, price, quantity
            if (fields.size() < min_full) {
                record.error = "ADD requires 6 data fields";
                return false;
            }

            // order_id
            record.order_id = 0;
            if (fields[oid_idx].empty()) {
                record.error = "ADD requires order_id";
                return false;
            }
            for (char c : fields[oid_idx]) {
                if (c < '0' || c > '9') {
                    record.error = "invalid order_id";
                    return false;
                }
                record.order_id = record.order_id * 10 + static_cast<uint64_t>(c - '0');
            }

            // side
            bool side_ok = false;
            record.side = parse_side(fields[side_idx], side_ok);
            if (!side_ok) {
                record.error = "invalid side: " + std::string(fields[side_idx]);
                return false;
            }

            // price
            record.price = parse_price(fields[px_idx]);
            if (record.price == 0 && !fields[px_idx].empty() && fields[px_idx] != "0") {
                record.error = "invalid price: " + std::string(fields[px_idx]);
                return false;
            }

            // quantity
            record.quantity = parse_quantity(fields[qty_idx]);
            if (record.quantity == 0) {
                record.error = "invalid quantity: " + std::string(fields[qty_idx]);
                return false;
            }

            break;
        }

        case L3EventType::Cancel: {
            // Need: order_id; others optional/empty
            if (fields.size() < min_cancel) {
                record.error = "CANCEL requires at least 3 data fields";
                return false;
            }

            // order_id
            record.order_id = 0;
            if (fields[oid_idx].empty()) {
                record.error = "CANCEL requires order_id";
                return false;
            }
            for (char c : fields[oid_idx]) {
                if (c < '0' || c > '9') {
                    record.error = "invalid order_id";
                    return false;
                }
                record.order_id = record.order_id * 10 + static_cast<uint64_t>(c - '0');
            }

            break;
        }

        case L3EventType::Trade: {
            // Informational: side, price, quantity
            // order_id is empty/optional for trades
            if (fields.size() < min_full) {
                record.error = "TRADE requires 6 data fields";
                return false;
            }

            // side (optional for trades, but parse if present)
            if (!fields[side_idx].empty()) {
                bool side_ok = false;
                record.side = parse_side(fields[side_idx], side_ok);
                // Side is informational for TRADE — don't fail on invalid
            }

            // price
            if (!fields[px_idx].empty()) {
                record.price = parse_price(fields[px_idx]);
            }

            // quantity
            if (!fields[qty_idx].empty()) {
                record.quantity = parse_quantity(fields[qty_idx]);
            }

            break;
        }

        case L3EventType::Modify: {
            // Same fields as ADD: order_id, side, price, quantity
            if (fields.size() < min_full) {
                record.error = "MODIFY requires 6 data fields";
                return false;
            }

            // order_id
            record.order_id = 0;
            if (fields[oid_idx].empty()) {
                record.error = "MODIFY requires order_id";
                return false;
            }
            for (char c : fields[oid_idx]) {
                if (c < '0' || c > '9') {
                    record.error = "invalid order_id";
                    return false;
                }
                record.order_id = record.order_id * 10 + static_cast<uint64_t>(c - '0');
            }

            // side
            bool side_ok = false;
            record.side = parse_side(fields[side_idx], side_ok);
            if (!side_ok) {
                record.error = "invalid side: " + std::string(fields[side_idx]);
                return false;
            }

            // price
            record.price = parse_price(fields[px_idx]);
            if (record.price == 0 && !fields[px_idx].empty() && fields[px_idx] != "0") {
                record.error = "invalid price: " + std::string(fields[px_idx]);
                return false;
            }

            // quantity
            record.quantity = parse_quantity(fields[qty_idx]);
            if (record.quantity == 0) {
                record.error = "invalid quantity: " + std::string(fields[qty_idx]);
                return false;
            }

            break;
        }

        case L3EventType::Invalid:
            // Already handled above
            break;
    }

    record.valid = true;
    return true;
}

bool L3FeedParser::is_header(std::string_view line) {
    // Check if the line starts with common header words (case-insensitive)
    if (line.size() < 4) return false;

    auto fields = split_csv(line);
    if (fields.empty()) return false;

    // Lower-case first field
    std::string first(fields[0]);
    for (auto& c : first) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }

    return first == "timestamp" || first == "symbol";
}

}  // namespace hft
