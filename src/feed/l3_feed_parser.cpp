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
    for (size_t i = 0; i < str.size(); ++i) {
        if (str[i] < '0' || str[i] > '9') return 0;
        result = result * 10 + static_cast<uint64_t>(str[i] - '0');
    }
    return result;
}

OrderMessage L3FeedParser::to_order_message(const L3Record& record) {
    OrderMessage msg{};
    msg.type = MessageType::Add;
    msg.order.order_id = record.order_id;
    msg.order.participant_id = 0;  // L3 feed doesn't carry participant ID
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

    // Field 0: timestamp
    if (fields[0].empty()) {
        record.error = "empty timestamp";
        return false;
    }
    record.timestamp = 0;
    for (size_t i = 0; i < fields[0].size(); ++i) {
        char c = fields[0][i];
        if (c < '0' || c > '9') {
            record.error = "invalid timestamp";
            return false;
        }
        record.timestamp = record.timestamp * 10 + static_cast<uint64_t>(c - '0');
    }

    // Field 1: event_type
    record.event_type = parse_event_type(fields[1]);
    if (record.event_type == L3EventType::Invalid) {
        record.error = "invalid event type: " + std::string(fields[1]);
        return false;
    }

    // Remaining fields depend on event type
    switch (record.event_type) {
        case L3EventType::Add: {
            // Need: order_id, side, price, quantity (fields 2-5)
            if (fields.size() < 6) {
                record.error = "ADD requires 6 fields";
                return false;
            }

            // order_id
            record.order_id = 0;
            if (fields[2].empty()) {
                record.error = "ADD requires order_id";
                return false;
            }
            for (size_t i = 0; i < fields[2].size(); ++i) {
                char c = fields[2][i];
                if (c < '0' || c > '9') {
                    record.error = "invalid order_id";
                    return false;
                }
                record.order_id = record.order_id * 10 + static_cast<uint64_t>(c - '0');
            }

            // side
            bool side_ok = false;
            record.side = parse_side(fields[3], side_ok);
            if (!side_ok) {
                record.error = "invalid side: " + std::string(fields[3]);
                return false;
            }

            // price
            record.price = parse_price(fields[4]);
            if (record.price == 0 && !fields[4].empty() && fields[4] != "0") {
                record.error = "invalid price: " + std::string(fields[4]);
                return false;
            }

            // quantity
            record.quantity = parse_quantity(fields[5]);
            if (record.quantity == 0) {
                record.error = "invalid quantity: " + std::string(fields[5]);
                return false;
            }

            break;
        }

        case L3EventType::Cancel: {
            // Need: order_id (field 2); others optional/empty
            if (fields.size() < 3) {
                record.error = "CANCEL requires at least 3 fields";
                return false;
            }

            // order_id
            record.order_id = 0;
            if (fields[2].empty()) {
                record.error = "CANCEL requires order_id";
                return false;
            }
            for (size_t i = 0; i < fields[2].size(); ++i) {
                char c = fields[2][i];
                if (c < '0' || c > '9') {
                    record.error = "invalid order_id";
                    return false;
                }
                record.order_id = record.order_id * 10 + static_cast<uint64_t>(c - '0');
            }

            break;
        }

        case L3EventType::Trade: {
            // Informational: side, price, quantity (fields 3-5)
            // order_id is empty/optional for trades
            if (fields.size() < 6) {
                record.error = "TRADE requires 6 fields";
                return false;
            }

            // side (optional for trades, but parse if present)
            if (!fields[3].empty()) {
                bool side_ok = false;
                record.side = parse_side(fields[3], side_ok);
                // Side is informational for TRADE — don't fail on invalid
            }

            // price
            if (!fields[4].empty()) {
                record.price = parse_price(fields[4]);
            }

            // quantity
            if (!fields[5].empty()) {
                record.quantity = parse_quantity(fields[5]);
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

    // Check for "timestamp" at the start
    std::string prefix(line.substr(0, 9));
    for (auto& c : prefix) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return prefix == "timestamp";
}

}  // namespace hft
