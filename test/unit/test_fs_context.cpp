#include "../framework/framework.hpp"
#include "cinux/memory.hpp"
#include "cinux/shared.hpp"
#include "kernel/fs/fs_context.hpp"
#include "test_assert.hpp"

namespace {

TEST("fs context: a fresh context stands at the root with the default mask") {
    cinux::base::Shared<cinux::fs::FsContext> context =
        cinux::base::MakeShared<cinux::fs::FsContext>();
    ASSERT_TRUE(context.holds());
    ASSERT_EQ(context->cwd[0], '/');
    ASSERT_EQ(context->root[0], '/');
    ASSERT_EQ(context->umask, 0022U);
}

TEST("fs context: a forked copy carries its own paths and mask") {
    cinux::base::Shared<cinux::fs::FsContext> parent =
        cinux::base::MakeShared<cinux::fs::FsContext>();
    cinux::base::CopyBytes(parent->cwd, "/home/guest", sizeof("/home/guest"));
    parent->umask = 0077U;
    cinux::base::Shared<cinux::fs::FsContext> child = cinux::fs::CloneFsContext(*parent);
    ASSERT_TRUE(child.holds());
    ASSERT_TRUE(child.get() != parent.get());
    ASSERT_TRUE(cinux::base::EqualBytes(child->cwd, "/home/guest", sizeof("/home/guest")));
    ASSERT_EQ(child->umask, 0077U);
    cinux::base::CopyBytes(child->cwd, "/tmp", sizeof("/tmp"));
    ASSERT_TRUE(cinux::base::EqualBytes(parent->cwd, "/home/guest", sizeof("/home/guest")));
}

TEST("fs context: a shared hold sees the other's chdir") {
    cinux::base::Shared<cinux::fs::FsContext> maker =
        cinux::base::MakeShared<cinux::fs::FsContext>();
    cinux::base::Shared<cinux::fs::FsContext> thread = maker;
    ASSERT_EQ(thread->cwd[0], '/');
    ASSERT_EQ(thread->cwd[1], '\0');
    cinux::base::CopyBytes(maker->cwd, "/var/log", sizeof("/var/log"));
    ASSERT_EQ(thread->cwd[0], '/');
    ASSERT_EQ(thread->cwd[1], 'v');
    ASSERT_TRUE(maker->use_count() == 2U);
}

}  // namespace

int main() {
    return cinux::test::RunAll();
}
