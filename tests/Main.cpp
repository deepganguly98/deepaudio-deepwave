#include <juce_core/juce_core.h>

// Runs every juce::UnitTest; exit code 1 on any failure.
int main()
{
    juce::UnitTestRunner runner;
    runner.setAssertOnFailure (false);
    runner.runAllTests();

    int failures = 0;
    for (int i = 0; i < runner.getNumResults(); ++i)
        failures += runner.getResult (i)->failures;

    std::cout << (failures == 0 ? "\nALL TESTS PASSED\n" : "\nTESTS FAILED: " + juce::String (failures) + " failure(s)\n");
    return failures == 0 ? 0 : 1;
}
