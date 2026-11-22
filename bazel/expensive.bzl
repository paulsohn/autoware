"""Constraints for costly targets that require --config=expensive."""

EXPENSIVE_TARGET_COMPATIBILITY = select({
    "//bazel:expensive_targets_enabled": [],
    "//conditions:default": ["@platforms//:incompatible"],
})
