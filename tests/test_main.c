#include <cgreen/cgreen.h>
#include <cgreen/unit.h>

TestSuite *create_path_validation_test_suite(void);

int main(int argc, char **argv) {
    TestSuite *suite = create_test_suite();
    add_suite(suite, create_path_validation_test_suite());
    return run_test_suite(suite, create_text_reporter());
}