/*
 * @test /nodynamiccopyright/
 * @bug 8160196
 * @summary Module summary page should display information based on "api" or "detail" mode.
 * @modules jdk.compiler/com.sun.tools.doclint
 * @build DocLintTester
 * @run main DocLintTester -ref ProvidesTest.out ProvidesTest.java
 */

/**
 * Invalid use of provides in class documentation.
 *
 * @provides UsesTest
 */
public class ProvidesTest {
    /**
     * Invalid use of provides in field documentation
     *
     * @provides UsesTest Test description.
     */
    public int invalid_param;

    /**
     * Invalid use of provides in method documentation
     *
     * @provides UsesTest Test description.
     */
    public class InvalidParam { }
}