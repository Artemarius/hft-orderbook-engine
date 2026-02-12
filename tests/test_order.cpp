#include <gtest/gtest.h>

#include <cstring>
#include <type_traits>

#include "core/order.h"
#include "core/price_level.h"
#include "core/trade.h"
#include "core/types.h"

namespace hft {
namespace {

// ---------------------------------------------------------------------------
// Types / enums
// ---------------------------------------------------------------------------

TEST(TypesTest, SideValues) {
    EXPECT_EQ(static_cast<uint8_t>(Side::Buy), 0);
    EXPECT_EQ(static_cast<uint8_t>(Side::Sell), 1);
}

TEST(TypesTest, OrderTypeValues) {
    EXPECT_NE(static_cast<uint8_t>(OrderType::Limit),
              static_cast<uint8_t>(OrderType::Market));
    EXPECT_NE(static_cast<uint8_t>(OrderType::IOC),
              static_cast<uint8_t>(OrderType::FOK));
}

TEST(TypesTest, PriceScaleIsCorrect) {
    EXPECT_EQ(PRICE_SCALE, 100'000'000);
}

TEST(TypesTest, FixedPointPriceConversion) {
    // 50000.50 USD -> fixed point
    Price price = 50000 * PRICE_SCALE + PRICE_SCALE / 2;
    EXPECT_EQ(price, 5'000'050'000'000);
}

TEST(TypesTest, TypeSizesAreMinimal) {
    EXPECT_EQ(sizeof(Side), 1);
    EXPECT_EQ(sizeof(OrderType), 1);
    EXPECT_EQ(sizeof(TimeInForce), 1);
    EXPECT_EQ(sizeof(OrderStatus), 1);
    EXPECT_EQ(sizeof(Price), 8);
    EXPECT_EQ(sizeof(Quantity), 8);
    EXPECT_EQ(sizeof(OrderId), 8);
    EXPECT_EQ(sizeof(ParticipantId), 4);
    EXPECT_EQ(sizeof(InstrumentId), 4);
    EXPECT_EQ(sizeof(Timestamp), 8);
}

TEST(TypesTest, EnumsAreScoped) {
    EXPECT_TRUE((std::is_enum_v<Side>));
    EXPECT_TRUE((std::is_enum_v<OrderType>));
    EXPECT_TRUE((std::is_enum_v<TimeInForce>));
    EXPECT_TRUE((std::is_enum_v<OrderStatus>));
}

// ---------------------------------------------------------------------------
// Order struct
// ---------------------------------------------------------------------------

TEST(OrderTest, IsTriviallyCopyable) {
    EXPECT_TRUE(std::is_trivially_copyable_v<Order>);
}

TEST(OrderTest, IsStandardLayout) {
    EXPECT_TRUE(std::is_standard_layout_v<Order>);
}

TEST(OrderTest, FitsInTwoCacheLines) {
    EXPECT_LE(sizeof(Order), 128u);
}

TEST(OrderTest, FieldLayout) {
    // Verify there's no unexpected padding blowing up the size.
    // 8 + 4 + 4 + 1 + 1 + 1 + 1 + [4 pad] + 8 + 8 + 8 + 8 + 8 + 8 + 8 + 8 = 88
    // Includes instrument_id and iceberg_slice_qty fields.
    EXPECT_LE(sizeof(Order), 96u);
}

TEST(OrderTest, MemcpySafe) {
    Order a{};
    a.order_id = 42;
    a.price = 50000 * PRICE_SCALE;
    a.quantity = 100;
    a.filled_quantity = 0;
    a.side = Side::Buy;
    a.type = OrderType::Limit;
    a.time_in_force = TimeInForce::GTC;
    a.status = OrderStatus::New;
    a.next = nullptr;
    a.prev = nullptr;

    Order b{};
    std::memcpy(&b, &a, sizeof(Order));
    EXPECT_EQ(b.order_id, 42u);
    EXPECT_EQ(b.price, 50000 * PRICE_SCALE);
    EXPECT_EQ(b.quantity, 100u);
    EXPECT_EQ(b.side, Side::Buy);
}

TEST(OrderTest, RemainingQuantity) {
    Order o{};
    o.quantity = 1000;
    o.filled_quantity = 300;
    EXPECT_EQ(o.remaining_quantity(), 700u);
}

TEST(OrderTest, RemainingVisible) {
    Order o{};
    o.visible_quantity = 200;
    o.filled_quantity = 50;
    EXPECT_EQ(o.remaining_visible(), 150u);
}

// ---------------------------------------------------------------------------
// Trade struct
// ---------------------------------------------------------------------------

TEST(TradeTest, IsTriviallyCopyable) {
    EXPECT_TRUE(std::is_trivially_copyable_v<Trade>);
}

TEST(TradeTest, IsStandardLayout) {
    EXPECT_TRUE(std::is_standard_layout_v<Trade>);
}

TEST(TradeTest, ExactSize) {
    EXPECT_EQ(sizeof(Trade), 48u);
}

TEST(TradeTest, FieldValues) {
    Trade t{};
    t.trade_id = 1;
    t.buy_order_id = 10;
    t.sell_order_id = 20;
    t.price = 50000 * PRICE_SCALE;
    t.quantity = 500;
    t.timestamp = 1234567890;

    EXPECT_EQ(t.trade_id, 1u);
    EXPECT_EQ(t.buy_order_id, 10u);
    EXPECT_EQ(t.sell_order_id, 20u);
    EXPECT_EQ(t.price, 50000 * PRICE_SCALE);
    EXPECT_EQ(t.quantity, 500u);
    EXPECT_EQ(t.timestamp, 1234567890u);
}

// ---------------------------------------------------------------------------
// PriceLevel struct
// ---------------------------------------------------------------------------

TEST(PriceLevelTest, IsTriviallyCopyable) {
    EXPECT_TRUE(std::is_trivially_copyable_v<PriceLevel>);
}

TEST(PriceLevelTest, IsStandardLayout) {
    EXPECT_TRUE(std::is_standard_layout_v<PriceLevel>);
}

TEST(PriceLevelTest, EmptyLevel) {
    PriceLevel level{};
    level.price = 50000 * PRICE_SCALE;
    level.total_quantity = 0;
    level.order_count = 0;
    level.head = nullptr;
    level.tail = nullptr;

    EXPECT_TRUE(level.empty());
    EXPECT_EQ(level.front(), nullptr);
    EXPECT_EQ(level.order_count, 0u);
    EXPECT_EQ(level.total_quantity, 0u);
}

TEST(PriceLevelTest, AddSingleOrder) {
    Order o{};
    o.order_id = 1;
    o.quantity = 100;
    o.filled_quantity = 0;

    PriceLevel level{};
    level.price = 50000 * PRICE_SCALE;
    level.total_quantity = 0;
    level.order_count = 0;
    level.head = nullptr;
    level.tail = nullptr;

    level.add_order(&o);

    EXPECT_FALSE(level.empty());
    EXPECT_EQ(level.front(), &o);
    EXPECT_EQ(level.order_count, 1u);
    EXPECT_EQ(level.total_quantity, 100u);
    EXPECT_EQ(level.head, &o);
    EXPECT_EQ(level.tail, &o);
    EXPECT_EQ(o.prev, nullptr);
    EXPECT_EQ(o.next, nullptr);
}

TEST(PriceLevelTest, AddMultipleOrdersFIFO) {
    Order o1{}, o2{}, o3{};
    o1.order_id = 1;
    o1.quantity = 100;
    o1.filled_quantity = 0;
    o2.order_id = 2;
    o2.quantity = 200;
    o2.filled_quantity = 0;
    o3.order_id = 3;
    o3.quantity = 300;
    o3.filled_quantity = 0;

    PriceLevel level{};
    level.price = 50000 * PRICE_SCALE;
    level.total_quantity = 0;
    level.order_count = 0;
    level.head = nullptr;
    level.tail = nullptr;

    level.add_order(&o1);
    level.add_order(&o2);
    level.add_order(&o3);

    EXPECT_EQ(level.order_count, 3u);
    EXPECT_EQ(level.total_quantity, 600u);

    // FIFO: o1 is first (oldest), o3 is last
    EXPECT_EQ(level.front(), &o1);
    EXPECT_EQ(level.head, &o1);
    EXPECT_EQ(level.tail, &o3);

    // Linked list traversal
    EXPECT_EQ(o1.next, &o2);
    EXPECT_EQ(o2.next, &o3);
    EXPECT_EQ(o3.next, nullptr);
    EXPECT_EQ(o3.prev, &o2);
    EXPECT_EQ(o2.prev, &o1);
    EXPECT_EQ(o1.prev, nullptr);
}

TEST(PriceLevelTest, RemoveHead) {
    Order o1{}, o2{};
    o1.order_id = 1;
    o1.quantity = 100;
    o1.filled_quantity = 0;
    o2.order_id = 2;
    o2.quantity = 200;
    o2.filled_quantity = 0;

    PriceLevel level{};
    level.price = 50000 * PRICE_SCALE;
    level.total_quantity = 0;
    level.order_count = 0;
    level.head = nullptr;
    level.tail = nullptr;

    level.add_order(&o1);
    level.add_order(&o2);
    level.remove_order(&o1);

    EXPECT_EQ(level.order_count, 1u);
    EXPECT_EQ(level.total_quantity, 200u);
    EXPECT_EQ(level.front(), &o2);
    EXPECT_EQ(level.head, &o2);
    EXPECT_EQ(level.tail, &o2);
    EXPECT_EQ(o2.prev, nullptr);
    EXPECT_EQ(o2.next, nullptr);
}

TEST(PriceLevelTest, RemoveTail) {
    Order o1{}, o2{};
    o1.order_id = 1;
    o1.quantity = 100;
    o1.filled_quantity = 0;
    o2.order_id = 2;
    o2.quantity = 200;
    o2.filled_quantity = 0;

    PriceLevel level{};
    level.price = 50000 * PRICE_SCALE;
    level.total_quantity = 0;
    level.order_count = 0;
    level.head = nullptr;
    level.tail = nullptr;

    level.add_order(&o1);
    level.add_order(&o2);
    level.remove_order(&o2);

    EXPECT_EQ(level.order_count, 1u);
    EXPECT_EQ(level.total_quantity, 100u);
    EXPECT_EQ(level.front(), &o1);
    EXPECT_EQ(level.head, &o1);
    EXPECT_EQ(level.tail, &o1);
    EXPECT_EQ(o1.prev, nullptr);
    EXPECT_EQ(o1.next, nullptr);
}

TEST(PriceLevelTest, RemoveMiddle) {
    Order o1{}, o2{}, o3{};
    o1.order_id = 1;
    o1.quantity = 100;
    o1.filled_quantity = 0;
    o2.order_id = 2;
    o2.quantity = 200;
    o2.filled_quantity = 0;
    o3.order_id = 3;
    o3.quantity = 300;
    o3.filled_quantity = 0;

    PriceLevel level{};
    level.price = 50000 * PRICE_SCALE;
    level.total_quantity = 0;
    level.order_count = 0;
    level.head = nullptr;
    level.tail = nullptr;

    level.add_order(&o1);
    level.add_order(&o2);
    level.add_order(&o3);
    level.remove_order(&o2);

    EXPECT_EQ(level.order_count, 2u);
    EXPECT_EQ(level.total_quantity, 400u);
    EXPECT_EQ(level.head, &o1);
    EXPECT_EQ(level.tail, &o3);
    EXPECT_EQ(o1.next, &o3);
    EXPECT_EQ(o3.prev, &o1);
}

TEST(PriceLevelTest, RemoveOnlyOrder) {
    Order o1{};
    o1.order_id = 1;
    o1.quantity = 100;
    o1.filled_quantity = 0;

    PriceLevel level{};
    level.price = 50000 * PRICE_SCALE;
    level.total_quantity = 0;
    level.order_count = 0;
    level.head = nullptr;
    level.tail = nullptr;

    level.add_order(&o1);
    level.remove_order(&o1);

    EXPECT_TRUE(level.empty());
    EXPECT_EQ(level.front(), nullptr);
    EXPECT_EQ(level.order_count, 0u);
    EXPECT_EQ(level.total_quantity, 0u);
    EXPECT_EQ(level.head, nullptr);
    EXPECT_EQ(level.tail, nullptr);
}

TEST(PriceLevelTest, QuantityTracksPartialFills) {
    Order o{};
    o.order_id = 1;
    o.quantity = 1000;
    o.filled_quantity = 0;

    PriceLevel level{};
    level.price = 50000 * PRICE_SCALE;
    level.total_quantity = 0;
    level.order_count = 0;
    level.head = nullptr;
    level.tail = nullptr;

    level.add_order(&o);
    EXPECT_EQ(level.total_quantity, 1000u);

    // Simulate a partial fill as the matching engine would:
    // 1. Decrement total_quantity by the fill amount
    // 2. Update the order's filled_quantity
    Quantity fill_qty = 400;
    level.total_quantity -= fill_qty;
    o.filled_quantity += fill_qty;
    EXPECT_EQ(level.total_quantity, 600u);
    EXPECT_EQ(o.remaining_quantity(), 600u);

    // Now remove the partially filled order — subtracts remaining_quantity
    level.remove_order(&o);
    EXPECT_EQ(level.total_quantity, 0u);
    EXPECT_TRUE(level.empty());
}

}  // namespace
}  // namespace hft
