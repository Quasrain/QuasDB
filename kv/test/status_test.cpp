#include "kv/include/status.h"

#include <utility>

#include "gtest/gtest.h"
#include "kv/include/slice.h"

namespace QuasDB
{

  TEST(Status, MoveConstructor)
  {
    {
      Status ok = Status::OK();
      Status ok2 = std::move(ok);

      ASSERT_TRUE(ok2.ok());
    }

    {
      Status status = Status::NotFound("custom NotFound status message");
      Status status2 = std::move(status);

      ASSERT_TRUE(status2.IsNotFound());
      ASSERT_EQ("NotFound: custom NotFound status message", status2.ToString());
    }

    {
      Status self_moved = Status::IOError("custom IOError status message");

      // Needed to bypass compiler warning about explicit move-assignment.
      Status &self_moved_reference = self_moved;
      self_moved_reference = std::move(self_moved);
    }
  }

} // namespace QuasDB

int main(int argc, char **argv)
{
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
