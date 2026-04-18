#include <cgreen/cgreen.h>

int is_safe_path(const char *path);

Describe(PathValidation);

BeforeEach(PathValidation) {}
AfterEach(PathValidation) {}

Ensure(PathValidation, accepts_normal_file_path) {
    assert_that(is_safe_path("/index.html"), is_equal_to(1));
}

Ensure(PathValidation, rejects_parent_directory_traversal) {
    assert_that(is_safe_path("/../secret.txt"), is_equal_to(0));
}

Ensure(PathValidation, rejects_double_dot_inside_path) {
    assert_that(is_safe_path("/images/../../etc/passwd"), is_equal_to(0));
}

Ensure(PathValidation, rejects_null_path) {
    assert_that(is_safe_path(NULL), is_equal_to(0));
}

Ensure(PathValidation, accepts_nested_safe_path) {
    assert_that(is_safe_path("/images/logo.png"), is_equal_to(1));
}

Ensure(PathValidation, rejects_safe_parent_reference_pattern) {
    assert_that(is_safe_path("/safe/../file.txt"), is_equal_to(0));
}

Ensure(PathValidation, accepts_empty_string_current_behavior) {
    assert_that(is_safe_path(""), is_equal_to(1));
}

TestSuite *create_path_validation_test_suite(void) {
    TestSuite *suite = create_test_suite();

    add_test_with_context(suite, PathValidation, accepts_normal_file_path);
    add_test_with_context(suite, PathValidation, rejects_parent_directory_traversal);
    add_test_with_context(suite, PathValidation, rejects_double_dot_inside_path);
    add_test_with_context(suite, PathValidation, rejects_null_path);
    add_test_with_context(suite, PathValidation, accepts_nested_safe_path);
    add_test_with_context(suite, PathValidation, rejects_safe_parent_reference_pattern);
    add_test_with_context(suite, PathValidation, accepts_empty_string_current_behavior);

    return suite;
}


Ensure(PathValidation, rejects_only_double_dot) {
    assert_that(is_safe_path(".."), is_equal_to(0));
}

Ensure(PathValidation, accepts_three_dots_in_name) {
    assert_that(is_safe_path("/file...txt"), is_equal_to(1));
}

Ensure(PathValidation, rejects_embedded_double_dot_name_current_behavior) {
    assert_that(is_safe_path("/images/..hidden/file.txt"), is_equal_to(0));
}