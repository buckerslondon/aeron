/*
 * Copyright 2014-2020 Real Logic Limited.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
package io.aeron.test;

import org.agrona.IoUtil;
import org.agrona.collections.Object2ObjectHashMap;
import org.junit.jupiter.api.extension.ExtensionContext;
import org.junit.jupiter.api.extension.TestWatcher;

import java.io.File;
import java.io.IOException;
import java.io.PrintStream;
import java.nio.file.Files;
import java.util.Map;

public class MediaDriverTestWatcher implements TestWatcher, DriverOutputConsumer
{
    private final Map<String, ProcessDetails> outputFilesByAeronDirectoryName = new Object2ObjectHashMap<>();

    public void testFailed(final ExtensionContext context, final Throwable cause)
    {
        if (TestMediaDriver.shouldRunCMediaDriver())
        {
            System.out.println("C Media Driver tests failed");
            outputFilesByAeronDirectoryName.forEach(
                (aeronDirectoryName, files) ->
                {
                    try
                    {
                        System.out.println();
                        System.out.println("Media Driver: " + aeronDirectoryName + ", exit code: " + files.exitValue);
                        printEnvironment(files.environment, System.out);
                        System.out.println("*** STDOUT ***");
                        Files.copy(files.stdout.toPath(), System.out);
                        System.out.println("*** STDERR ***");
                        Files.copy(files.stderr.toPath(), System.out);
                        System.out.println("====");
                    }
                    catch (final IOException ex)
                    {
                        throw new RuntimeException("Failed to output logs to stdout", ex);
                    }
                });

            deleteFiles();
        }
    }

    private void printEnvironment(final Map<String, String> environment, final PrintStream ps)
    {
        environment.forEach(
            (name, value) ->
            {
                ps.print(name);
                ps.print('=');
                ps.print(value);
                ps.println();
            });
    }

    public void testSuccessful(final ExtensionContext context)
    {
        if (TestMediaDriver.shouldRunCMediaDriver())
        {
            deleteFiles();
        }
    }

    private void deleteFiles()
    {
        outputFilesByAeronDirectoryName.forEach(
            (aeronDirectoryName, files) ->
            {
                IoUtil.delete(files.stdout, false);
                IoUtil.delete(files.stderr, false);
            });
    }

    public void outputFiles(final String aeronDirectoryName, final File stdoutFile, final File stderrFile)
    {
        outputFilesByAeronDirectoryName.put(aeronDirectoryName, new ProcessDetails(stdoutFile, stderrFile));
    }

    public void exitCode(final String aeronDirectoryName, final int exitValue)
    {
        outputFilesByAeronDirectoryName.get(aeronDirectoryName).exitValue(exitValue);
    }

    public void environmentVariables(final String aeronDirectoryName, final Map<String, String> environment)
    {
        outputFilesByAeronDirectoryName.get(aeronDirectoryName).environment(environment);
    }

    static final class ProcessDetails
    {
        private final File stderr;
        private final File stdout;
        private int exitValue;
        private Map<String, String> environment;

        ProcessDetails(final File stdout, final File stderr)
        {
            this.stderr = stderr;
            this.stdout = stdout;
        }

        public void exitValue(final int exitValue)
        {
            this.exitValue = exitValue;
        }

        public void environment(final Map<String, String> environment)
        {
            this.environment = environment;
        }
    }
}
