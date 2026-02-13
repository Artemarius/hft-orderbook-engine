#pragma once

/// @file fix_serializer.h
/// @brief FIX 4.2 outbound serializer — EventMessage to Execution Report.
///
/// Cold-path component. Converts the engine's internal EventMessage (Trade,
/// OrderAccepted, OrderCancelled, etc.) into FIX 4.2 Execution Report
/// messages (35=8) for downstream consumers.

#include <string>

#include "core/types.h"
#include "feed/fix_message.h"
#include "transport/message.h"

namespace hft {
namespace fix {

class FixSerializer {
public:
    /// Serialize an EventMessage to a FIX 4.2 Execution Report (SOH-delimited).
    /// @param event   The engine event to serialize.
    /// @param symbol  Instrument symbol for tag 55 (default: "N/A").
    /// @param sender  SenderCompID for tag 49 (default: "HFT-ENGINE").
    /// @param target  TargetCompID for tag 56 (default: "CLIENT").
    static std::string to_execution_report(
        const EventMessage& event,
        const std::string& symbol = "N/A",
        const std::string& sender = "HFT-ENGINE",
        const std::string& target = "CLIENT");

    /// Same as to_execution_report but pipe-delimited for human readability.
    static std::string to_execution_report_pretty(
        const EventMessage& event,
        const std::string& symbol = "N/A",
        const std::string& sender = "HFT-ENGINE",
        const std::string& target = "CLIENT");

    /// Map engine Side enum to FIX Side char ('1'/'2').
    static char to_fix_side(Side side);

    /// Map engine EventType to FIX ExecType char (tag 150).
    static char to_fix_exec_type(EventType type);

    /// Map engine EventType to FIX OrdStatus char (tag 39).
    static char to_fix_ord_status(EventType type);

    /// Format a fixed-point price back to a decimal string.
    /// E.g., 4200050000000 (42000.50 * 10^8) -> "42000.50000000"
    static std::string format_price(Price price);
};

}  // namespace fix
}  // namespace hft
