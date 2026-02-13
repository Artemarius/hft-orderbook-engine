#pragma once

/// @file fix_message.h
/// @brief FIX 4.2 protocol types, tag constants, and parsed message struct.
///
/// Cold-path component. Defines the ~20 FIX tags used by the parser and
/// serializer, message type constants, FIX-specific enum values, and the
/// FixMessage struct that holds a parsed FIX message before conversion to
/// the engine's internal OrderMessage format.

#include <string>

#include "core/types.h"

namespace hft {
namespace fix {

// ---------------------------------------------------------------------------
// FIX tag numbers (constexpr int for flat-array lookup)
// ---------------------------------------------------------------------------

namespace Tag {
constexpr int BeginString   = 8;
constexpr int BodyLength    = 9;
constexpr int CheckSum      = 10;
constexpr int ClOrdID       = 11;
constexpr int CumQty        = 14;
constexpr int ExecID        = 17;
constexpr int LastPx        = 31;
constexpr int LastQty       = 32;
constexpr int MsgType       = 35;
constexpr int OrderID       = 37;
constexpr int OrderQty      = 38;
constexpr int OrdStatus     = 39;
constexpr int OrdType       = 40;
constexpr int OrigClOrdID   = 41;
constexpr int Price         = 44;
constexpr int SenderCompID  = 49;
constexpr int Side          = 54;
constexpr int Symbol        = 55;
constexpr int TargetCompID  = 56;
constexpr int TimeInForce   = 59;
constexpr int TransactTime  = 60;
constexpr int ExecType      = 150;
constexpr int LeavesQty     = 151;
}  // namespace Tag

// ---------------------------------------------------------------------------
// FIX message type constants (tag 35 values)
// ---------------------------------------------------------------------------

namespace MsgType {
constexpr char NewOrderSingle    = 'D';
constexpr char OrderCancelRequest = 'F';
constexpr char OrderCancelReplace = 'G';
constexpr char ExecutionReport   = '8';
}  // namespace MsgType

// ---------------------------------------------------------------------------
// FIX enum values (single-char fields)
// ---------------------------------------------------------------------------

namespace SideValue {
constexpr char Buy  = '1';
constexpr char Sell = '2';
}  // namespace SideValue

namespace OrdTypeValue {
constexpr char Market = '1';
constexpr char Limit  = '2';
}  // namespace OrdTypeValue

namespace TifValue {
constexpr char Day = '0';
constexpr char GTC = '1';
constexpr char IOC = '3';
constexpr char FOK = '4';
}  // namespace TifValue

namespace ExecTypeValue {
constexpr char New         = '0';
constexpr char PartialFill = '1';
constexpr char Fill        = '2';
constexpr char Cancelled   = '4';
constexpr char Replaced    = '5';
constexpr char Rejected    = '8';
}  // namespace ExecTypeValue

namespace OrdStatusValue {
constexpr char New         = '0';
constexpr char PartialFill = '1';
constexpr char Filled      = '2';
constexpr char Cancelled   = '4';
constexpr char Replaced    = '5';
constexpr char Rejected    = '8';
}  // namespace OrdStatusValue

// ---------------------------------------------------------------------------
// Parsed FIX message
// ---------------------------------------------------------------------------

/// Intermediate representation of a parsed FIX message.
/// Populated by FixParser::parse(), consumed by FixParser::to_order_message().
struct FixMessage {
    char msg_type = '\0';               ///< Tag 35 value
    std::string begin_string;           ///< Tag 8 (e.g. "FIX.4.2")
    std::string sender_comp_id;         ///< Tag 49
    std::string target_comp_id;         ///< Tag 56
    std::string cl_ord_id;              ///< Tag 11
    std::string orig_cl_ord_id;         ///< Tag 41 (cancel/replace)
    std::string symbol;                 ///< Tag 55
    char fix_side = '\0';               ///< Tag 54
    char fix_ord_type = '\0';           ///< Tag 40
    char fix_tif = '\0';                ///< Tag 59
    Price price = 0;                    ///< Tag 44, converted to fixed-point
    Quantity quantity = 0;              ///< Tag 38
    std::string transact_time;          ///< Tag 60
    int body_length = 0;                ///< Tag 9 (declared body length)
    std::string checksum;               ///< Tag 10 (declared checksum)
    bool valid = false;                 ///< True if parsing succeeded
    std::string error;                  ///< Error description on failure
};

}  // namespace fix
}  // namespace hft
