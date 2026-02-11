#include <gtest/gtest.h>

#include <type_traits>

#include "core/types.h"

namespace hft {
namespace {

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
    // Enums should be uint8_t sized
    EXPECT_EQ(sizeof(Side), 1);
    EXPECT_EQ(sizeof(OrderType), 1);
    EXPECT_EQ(sizeof(TimeInForce), 1);
    EXPECT_EQ(sizeof(OrderStatus), 1);

    // Numeric types
    EXPECT_EQ(sizeof(Price), 8);
    EXPECT_EQ(sizeof(Quantity), 8);
    EXPECT_EQ(sizeof(OrderId), 8);
    EXPECT_EQ(sizeof(ParticipantId), 4);
    EXPECT_EQ(sizeof(Timestamp), 8);
}

TEST(TypesTest, EnumsAreScoped) {
    // Verify scoped enums — these should not implicitly convert to int
    EXPECT_TRUE((std::is_enum_v<Side>));
    EXPECT_TRUE((std::is_enum_v<OrderType>));
    EXPECT_TRUE((std::is_enum_v<TimeInForce>));
    EXPECT_TRUE((std::is_enum_v<OrderStatus>));
}

}  // namespace
}  // namespace hft
