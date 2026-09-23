#include <gtest/gtest.h>

#include <chrono>

#include "infrastructure/postgres/pg_utils.hpp"

namespace inerxia::infrastructure::postgres::test {
namespace {

using namespace std::chrono;

TEST(PgUtilsTest, SqlDatesRoundTrip) {
    EXPECT_EQ(to_sql_date(year{2026}/9/1), "2026-09-01");
    EXPECT_EQ(to_sql_date(year{2026}/12/31), "2026-12-31");
    EXPECT_EQ(from_sql_date("2026-09-01"), (year{2026}/9/1));
}

TEST(PgUtilsTest, InvalidSqlDatesAreRejected) {
    EXPECT_THROW(from_sql_date("2026-13-01"), PostgresError);
    EXPECT_THROW(from_sql_date("2026-02-30"), PostgresError);
    EXPECT_THROW(from_sql_date("2026/09/01"), PostgresError);
    EXPECT_THROW(from_sql_date("2026-9-1"), PostgresError);
    EXPECT_THROW(from_sql_date("banana"), PostgresError);
}

TEST(PgUtilsTest, UuidV4ShapeIsValid) {
    const std::string uuid = next_uuid_v4();
    ASSERT_EQ(uuid.size(), 36U);
    EXPECT_EQ(uuid[8], '-');
    EXPECT_EQ(uuid[13], '-');
    EXPECT_EQ(uuid[18], '-');
    EXPECT_EQ(uuid[23], '-');
    EXPECT_EQ(uuid[14], '4');
    const char variant = uuid[19];
    EXPECT_TRUE(variant == '8' || variant == '9' || variant == 'a' || variant == 'b');
    for (const char c : uuid) {
        EXPECT_TRUE(c == '-' || (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'));
    }
}

TEST(PgUtilsTest, UuidV4IsRandomAcrossCalls) {
    const std::string a = next_uuid_v4();
    const std::string b = next_uuid_v4();
    EXPECT_NE(a, b);
}

}  // namespace
}  // namespace inerxia::infrastructure::postgres::test