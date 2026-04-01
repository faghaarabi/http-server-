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

TestSuite *create_path_validation_test_suite(void) {
    TestSuite *suite = create_test_suite();

    add_test_with_context(suite, PathValidation, accepts_normal_file_path);
    add_test_with_context(suite, PathValidation, rejects_parent_directory_traversal);
    add_test_with_context(suite, PathValidation, rejects_double_dot_inside_path);

    return suite;
}