#pragma once

/// @file l3_feed_parser.h
/// @brief CSV parser for L3 (order-by-order) market data feeds.
///
/// Cold-path component. Parses CSV files with the format:
///   timestamp,event_type,order_id,side,price,quantity
///
/// Price parsing uses integer arithmetic only (no stod) to avoid
/// floating-point rounding errors in fixed-point conversion.

#include <cstdint>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include "core/types.h"
#include "transport/message.h"

namespace hft {

/// Type of event in an L3 feed record.
enum class L3EventType : uint8_t { Add, Cancel, Trade, Modify, Invalid };

/// A single parsed record from an L3 CSV file.
struct L3Record {
    Timestamp timestamp;
    L3EventType event_type;
    OrderId order_id;
    Side side;
    Price price;          // fixed-point (PRICE_SCALE)
    Quantity quantity;
    bool valid;
    std::string error;
};

/// Streaming CSV parser for L3 market data files.
///
/// Usage:
///   L3FeedParser parser;
///   parser.open("data/btcusdt_l3_sample.csv");
///   L3Record record;
///   while (parser.next(record)) {
///       if (!record.valid) { handle error; continue; }
///       // process record
///   }
///   parser.close();
class L3FeedParser {
public:
    L3FeedParser() = default;
    ~L3FeedParser();

    L3FeedParser(const L3FeedParser&) = delete;
    L3FeedParser& operator=(const L3FeedParser&) = delete;

    /// Open a CSV file for reading. Returns false if the file cannot be opened.
    bool open(const std::string& path);

    /// Read the next record from the file. Returns false at EOF.
    /// On parse error, record.valid is false and record.error describes the issue.
    bool next(L3Record& record);

    /// Reset to the beginning of the file.
    void reset();

    /// Close the file.
    void close();

    /// Number of lines read so far (including header and error lines).
    [[nodiscard]] uint64_t lines_read() const { return lines_read_; }

    /// Number of lines that failed to parse.
    [[nodiscard]] uint64_t parse_errors() const { return parse_errors_; }

    // -- Static parsing utilities --

    /// Convert an L3Record (ADD) to an OrderMessage for the gateway.
    static OrderMessage to_order_message(const L3Record& record);

    /// Convert an L3Record (MODIFY) to an OrderMessage for the gateway.
    static OrderMessage to_modify_message(const L3Record& record);

    /// Parse a decimal price string to fixed-point int64_t.
    /// Uses integer arithmetic only — no stod.
    /// Returns 0 on parse failure.
    static Price parse_price(std::string_view str);

    /// Parse a quantity string to uint64_t.
    /// Returns 0 on parse failure.
    static Quantity parse_quantity(std::string_view str);

    /// Parse event type string (case-insensitive).
    static L3EventType parse_event_type(std::string_view str);

    /// Parse side string (case-insensitive).
    /// Returns Side::Buy on failure (caller should check event_type validity).
    static Side parse_side(std::string_view str, bool& ok);

    /// Split a CSV line into fields.
    static std::vector<std::string_view> split_csv(std::string_view line);

private:
    /// Parse a single line into an L3Record.
    bool parse_line(std::string_view line, L3Record& record);

    /// Check if a line is a header row.
    static bool is_header(std::string_view line);

    std::ifstream file_;
    std::string current_line_;
    uint64_t lines_read_ = 0;
    uint64_t parse_errors_ = 0;
};

}  // namespace hft
