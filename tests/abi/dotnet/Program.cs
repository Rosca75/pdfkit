// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The .NET side of the ABI tier. It declares every exported symbol of
// include/pdfkit/pdfkit.h with LibraryImport, loads pdfkit.dll from the path
// given, and calls each catalogue function over the files of a case list
// (tests/abi/run.py writes it: one line per file, its path, its open
// expectation and its id, tab-separated). It checks every status against the
// one the case allows, the JSON error object's code, failed, expected and fix
// on every refusal, that pdfkit_buffer_free zeroes each buffer, and that a
// written output opens again and reads back the metadata written into it.
// It stops at the first failure, naming the file and the function, and exits
// non-zero. Usage: PdfkitAbi <expected-abi-version> <path-to-pdfkit.dll> <cases.tsv>.

using System;
using System.Collections.Generic;
using System.IO;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Runtime.InteropServices.Marshalling;
using System.Text;
using System.Text.Json;

namespace Pdfkit.Abi;

// The pdfkit_buffer struct of include/pdfkit/pdfkit.h.
[StructLayout(LayoutKind.Sequential)]
internal struct PdfkitBuffer
{
    public IntPtr Data;
    public nuint Size;
    public IntPtr Reserved;
}

// Every exported symbol, in the order of the header.
internal static unsafe partial class Native
{
    private const string Library = "pdfkit";

    [LibraryImport(Library)]
    internal static partial uint pdfkit_abi_version();

    [LibraryImport(Library)]
    internal static partial int pdfkit_version(ref PdfkitBuffer output);

    [LibraryImport(Library)]
    internal static partial void pdfkit_buffer_free(ref PdfkitBuffer buffer);

    [LibraryImport(Library)]
    internal static partial void pdfkit_close(IntPtr document);

    [LibraryImport(Library)]
    internal static partial int pdfkit_edit_new(IntPtr document, out IntPtr edit, ref PdfkitBuffer output);

    [LibraryImport(Library)]
    internal static partial void pdfkit_edit_free(IntPtr edit);

    [LibraryImport(Library, StringMarshalling = StringMarshalling.Utf8)]
    internal static partial int pdfkit_open(byte* bytes, nuint size, string? options, out IntPtr document,
                                            ref PdfkitBuffer output);

    [LibraryImport(Library, StringMarshalling = StringMarshalling.Utf8)]
    internal static partial int pdfkit_inspect(IntPtr document, string? options, ref PdfkitBuffer output);

    [LibraryImport(Library, StringMarshalling = StringMarshalling.Utf8)]
    internal static partial int pdfkit_text(IntPtr document, string? options, ref PdfkitBuffer output);

    [LibraryImport(Library, StringMarshalling = StringMarshalling.Utf8)]
    internal static partial int pdfkit_images(IntPtr document, string? options, ref PdfkitBuffer output);

    [LibraryImport(Library, StringMarshalling = StringMarshalling.Utf8)]
    internal static partial int pdfkit_image_data(IntPtr document, string? identity, string? options,
                                                  ref PdfkitBuffer output);

    [LibraryImport(Library, StringMarshalling = StringMarshalling.Utf8)]
    internal static partial int pdfkit_metadata(IntPtr document, string? options, ref PdfkitBuffer output);

    [LibraryImport(Library, StringMarshalling = StringMarshalling.Utf8)]
    internal static partial int pdfkit_channels(IntPtr document, string? options, ref PdfkitBuffer output);

    [LibraryImport(Library, StringMarshalling = StringMarshalling.Utf8)]
    internal static partial int pdfkit_find(IntPtr document, string? values, ref PdfkitBuffer output);

    [LibraryImport(Library, StringMarshalling = StringMarshalling.Utf8)]
    internal static partial int pdfkit_redact_text(IntPtr edit, string? matches, ref PdfkitBuffer output);

    [LibraryImport(Library, StringMarshalling = StringMarshalling.Utf8)]
    internal static partial int pdfkit_redact_images(IntPtr edit, string? treatments, ref PdfkitBuffer output);

    [LibraryImport(Library, StringMarshalling = StringMarshalling.Utf8)]
    internal static partial int pdfkit_set_metadata(IntPtr edit, string? fields, ref PdfkitBuffer output);

    [LibraryImport(Library, StringMarshalling = StringMarshalling.Utf8)]
    internal static partial int pdfkit_select_pages(IntPtr edit, string? pages, ref PdfkitBuffer output);

    [LibraryImport(Library, StringMarshalling = StringMarshalling.Utf8)]
    internal static partial int pdfkit_write(IntPtr edit, string? options, ref PdfkitBuffer pdf,
                                             ref PdfkitBuffer output);

    [LibraryImport(Library, StringMarshalling = StringMarshalling.Utf8)]
    internal static partial int pdfkit_check(byte* bytes, nuint size, string? strings, ref PdfkitBuffer output);
}

// A failed expectation, named by the file and the function.
internal sealed class AbiFailure : Exception
{
    public AbiFailure(string file, string function, string message)
        : base($"{file}: {function}: {message}")
    {
    }
}

internal static class Status
{
    public const int Ok = 0;
    public const int InvalidArgument = 1;
    public const int Refused = 2;
    public const int SelfCheck = 3;
    public const int NotImplemented = 4;
}

internal sealed class Session
{
    private readonly HashSet<string> _implemented = new();
    private string _file = "";
    public int Calls { get; private set; }

    public void Version(uint expectedAbi)
    {
        _file = "(library)";
        uint abi = Native.pdfkit_abi_version();
        if (abi != expectedAbi)
        {
            throw new AbiFailure(_file, "pdfkit_abi_version", $"returned {abi}, expected {expectedAbi}");
        }
        var buffer = new PdfkitBuffer();
        int status = Native.pdfkit_version(ref buffer);
        using JsonDocument version = Json("pdfkit_version", Take("pdfkit_version", status, ref buffer, Status.Ok));
        if (version.RootElement.GetProperty("abi").GetUInt32() != expectedAbi)
        {
            throw new AbiFailure(_file, "pdfkit_version", "abi differs from pdfkit_abi_version");
        }
        foreach (JsonElement row in version.RootElement.GetProperty("catalogue").EnumerateArray())
        {
            if (row.GetProperty("implemented").GetBoolean())
            {
                _implemented.Add(row.GetProperty("symbol").GetString() ?? "");
            }
        }
        Console.WriteLine($"dotnet: abi {abi}, {_implemented.Count} catalogue functions implemented");
    }

    // The buffer's text after checking the status is one of allowed and that
    // freeing zeroes the buffer; a refusal's error object is checked here.
    private string Take(string function, int status, ref PdfkitBuffer buffer, params int[] allowed)
    {
        ++Calls;
        byte[] bytes = Bytes(function, ref buffer);
        string text = Encoding.UTF8.GetString(bytes);
        if (Array.IndexOf(allowed, status) < 0)
        {
            throw new AbiFailure(_file, function,
                $"status {status}, expected one of {string.Join(", ", allowed)}: {Shorten(text)}");
        }
        if (status != Status.Ok)
        {
            CheckError(function, text);
        }
        return text;
    }

    private byte[] Bytes(string function, ref PdfkitBuffer buffer)
    {
        if (buffer.Data == IntPtr.Zero && buffer.Size != 0)
        {
            throw new AbiFailure(_file, function, "a buffer with a size and no data");
        }
        var bytes = new byte[checked((int)buffer.Size)];
        if (bytes.Length > 0)
        {
            Marshal.Copy(buffer.Data, bytes, 0, bytes.Length);
        }
        Native.pdfkit_buffer_free(ref buffer);
        if (buffer.Data != IntPtr.Zero || buffer.Size != 0)
        {
            throw new AbiFailure(_file, function, "pdfkit_buffer_free left the buffer set");
        }
        return bytes;
    }

    private void CheckError(string function, string text)
    {
        using JsonDocument error = Json(function, text);
        foreach (string field in new[] { "code", "failed", "expected", "fix" })
        {
            if (!error.RootElement.TryGetProperty(field, out JsonElement value) ||
                value.ValueKind != JsonValueKind.String || string.IsNullOrEmpty(value.GetString()))
            {
                throw new AbiFailure(_file, function, $"the error object has no {field}: {Shorten(text)}");
            }
        }
    }

    private JsonDocument Json(string function, string text)
    {
        try
        {
            return JsonDocument.Parse(text);
        }
        catch (JsonException error)
        {
            throw new AbiFailure(_file, function, $"the answer is not JSON ({error.Message}): {Shorten(text)}");
        }
    }

    private static string Shorten(string text) => text.Length > 300 ? text[..300] + "..." : text;

    private string ErrorCode(string text)
    {
        using JsonDocument error = Json("error", text);
        return error.RootElement.GetProperty("code").GetString() ?? "";
    }

    // The status a not-implemented function returns, else ok.
    private int Expected(string symbol) => _implemented.Contains(symbol) ? Status.Ok : Status.NotImplemented;

    private unsafe IntPtr Open(byte[] bytes, out int status, out string text)
    {
        var buffer = new PdfkitBuffer();
        IntPtr document;
        fixed (byte* data = bytes)
        {
            status = Native.pdfkit_open(data, (nuint)bytes.Length, null, out document, ref buffer);
        }
        text = Take("pdfkit_open", status, ref buffer, Status.Ok, Status.Refused);
        return document;
    }

    public void Refusals()
    {
        _file = "(refusals)";
        byte[] junk = Encoding.ASCII.GetBytes("not a PDF at all");
        IntPtr document = Open(junk, out int status, out string text);
        if (status != Status.Refused || ErrorCode(text) != "not_pdf" || document != IntPtr.Zero)
        {
            throw new AbiFailure(_file, "pdfkit_open", $"junk bytes were not refused as not_pdf: {text}");
        }
        var buffer = new PdfkitBuffer();
        status = Native.pdfkit_inspect(IntPtr.Zero, null, ref buffer);
        Take("pdfkit_inspect", status, ref buffer, Status.InvalidArgument);
        status = Native.pdfkit_edit_new(IntPtr.Zero, out IntPtr edit, ref buffer);
        Take("pdfkit_edit_new", status, ref buffer, Status.InvalidArgument);
        if (edit != IntPtr.Zero)
        {
            throw new AbiFailure(_file, "pdfkit_edit_new", "a null document gave an edit handle");
        }
        // The lifecycle functions accept null.
        Native.pdfkit_close(IntPtr.Zero);
        Native.pdfkit_edit_free(IntPtr.Zero);
        Native.pdfkit_buffer_free(ref buffer);
    }

    public void File(string path, string expectOpen, string id)
    {
        _file = id;
        byte[] bytes = System.IO.File.ReadAllBytes(path);
        IntPtr document = Open(bytes, out int status, out string opened);
        if (expectOpen.StartsWith("refused:", StringComparison.Ordinal))
        {
            string code = expectOpen["refused:".Length..];
            if (status != Status.Refused || ErrorCode(opened) != code)
            {
                throw new AbiFailure(_file, "pdfkit_open", $"expected the refusal {code}, got status {status}");
            }
            return;
        }
        if (status != Status.Ok)
        {
            throw new AbiFailure(_file, "pdfkit_open", $"expected {expectOpen}, got status {status}: {opened}");
        }
        try
        {
            Read(document);
            Edit(document);
        }
        finally
        {
            Native.pdfkit_close(document);
        }
    }

    private void Read(IntPtr document)
    {
        var buffer = new PdfkitBuffer();
        int status = Native.pdfkit_inspect(document, null, ref buffer);
        using JsonDocument inspected = Json("pdfkit_inspect", Take("pdfkit_inspect", status, ref buffer, Status.Ok));
        int pages = inspected.RootElement.GetProperty("page_count").GetInt32();

        status = Native.pdfkit_text(document, "{\"pages\": [1], \"blocks\": false}", ref buffer);
        Take("pdfkit_text", status, ref buffer, Expected("pdfkit_text"));
        status = Native.pdfkit_text(document, $"{{\"pages\": [{pages + 1}]}}", ref buffer);
        Take("pdfkit_text", status, ref buffer, Status.InvalidArgument, Status.NotImplemented);

        status = Native.pdfkit_images(document, "{\"pages\": [1]}", ref buffer);
        using JsonDocument images = Json("pdfkit_images", Take("pdfkit_images", status, ref buffer, Status.Ok));
        foreach (JsonElement image in images.RootElement.GetProperty("images").EnumerateArray())
        {
            string identity = image.GetProperty("identity").GetString() ?? "";
            bool decodable = image.GetProperty("decodable").GetBoolean();
            status = Native.pdfkit_image_data(document, identity, null, ref buffer);
            byte[] data = Bytes("pdfkit_image_data", ref buffer);
            ++Calls;
            if (decodable && status == Status.Ok)
            {
                bool png = data.Length > 8 && data[0] == 0x89 && data[1] == (byte)'P';
                bool jpeg = data.Length > 2 && data[0] == 0xFF && data[1] == 0xD8;
                if (!png && !jpeg)
                {
                    throw new AbiFailure(_file, "pdfkit_image_data", $"{identity} is neither PNG nor JPEG");
                }
            }
            else if (decodable)
            {
                throw new AbiFailure(_file, "pdfkit_image_data", $"{identity} is decodable but status {status}");
            }
            else
            {
                CheckError("pdfkit_image_data", Encoding.UTF8.GetString(data));
            }
            break;
        }
        status = Native.pdfkit_image_data(document, "img:0000000000000000", null, ref buffer);
        Take("pdfkit_image_data", status, ref buffer, Status.InvalidArgument);

        status = Native.pdfkit_metadata(document, null, ref buffer);
        using JsonDocument metadata = Json("pdfkit_metadata", Take("pdfkit_metadata", status, ref buffer, Status.Ok));
        if (metadata.RootElement.GetProperty("fields").GetArrayLength() != 8)
        {
            throw new AbiFailure(_file, "pdfkit_metadata", "the answer does not hold the eight fields");
        }

        status = Native.pdfkit_channels(document, null, ref buffer);
        using JsonDocument channels = Json("pdfkit_channels", Take("pdfkit_channels", status, ref buffer, Status.Ok));
        if (channels.RootElement.GetProperty("channels").GetArrayLength() == 0)
        {
            throw new AbiFailure(_file, "pdfkit_channels", "no channel row");
        }

        status = Native.pdfkit_find(document,
            "{\"values\": [{\"text\": \"e\"}, {\"text\": \"Zebulon Quarterfield\"}], \"pages\": [1], \"context\": 2}",
            ref buffer);
        string found = Take("pdfkit_find", status, ref buffer, Expected("pdfkit_find"));
        if (_implemented.Contains("pdfkit_find"))
        {
            using JsonDocument answer = Json("pdfkit_find", found);
            JsonElement values = answer.RootElement.GetProperty("values");
            if (answer.RootElement.GetProperty("fingerprint").GetString()?.Length != 64 ||
                values.GetArrayLength() != 2)
            {
                throw new AbiFailure(_file, "pdfkit_find", "the answer lacks the fingerprint or a value");
            }
            foreach (JsonElement value in values.EnumerateArray())
            {
                if (value.GetProperty("count").GetInt32() != value.GetProperty("occurrences").GetArrayLength())
                {
                    throw new AbiFailure(_file, "pdfkit_find", "a count differs from its occurrences");
                }
                foreach (JsonElement occurrence in value.GetProperty("occurrences").EnumerateArray())
                {
                    if (occurrence.GetProperty("glued_before").ValueKind != JsonValueKind.String ||
                        occurrence.GetProperty("glued_after").ValueKind != JsonValueKind.String)
                    {
                        throw new AbiFailure(_file, "pdfkit_find", "an occurrence lacks its glued letters");
                    }
                }
                if (value.GetProperty("strings").ValueKind != JsonValueKind.Array)
                {
                    throw new AbiFailure(_file, "pdfkit_find", "a value lacks its strings");
                }
            }
            // ABI 2 folds only what is asked: the marker in lower case is found
            // with case folding and not without it.
            int[] counts = new int[2];
            string[] foldings = { "{}", "{\"case\": true}" };
            for (int i = 0; i < 2; ++i)
            {
                status = Native.pdfkit_find(document,
                    "{\"values\": [{\"text\": \"zebulon quarterfield\", \"folding\": " + foldings[i] +
                    "}], \"pages\": [1]}", ref buffer);
                using JsonDocument lower = Json("pdfkit_find", Take("pdfkit_find", status, ref buffer, Status.Ok));
                counts[i] = lower.RootElement.GetProperty("values")[0].GetProperty("count").GetInt32();
            }
            if (counts[0] != 0 || counts[1] == 0)
            {
                throw new AbiFailure(_file, "pdfkit_find",
                    $"the lower-case marker counts {counts[0]} and {counts[1]}, expected 0 unfolded and more with case folding");
            }
        }
        status = Native.pdfkit_find(document, "{\"values\": [{\"text\": \"\"}]}", ref buffer);
        Take("pdfkit_find", status, ref buffer, Status.InvalidArgument, Status.NotImplemented);
    }

    private unsafe void Edit(IntPtr document)
    {
        var buffer = new PdfkitBuffer();
        int status = Native.pdfkit_edit_new(document, out IntPtr edit, ref buffer);
        Take("pdfkit_edit_new", status, ref buffer, Status.Ok);
        try
        {
            const string word = "Zebulon Quarterfield";
            status = Native.pdfkit_redact_text(edit,
                $"{{\"matches\": [{{\"search\": \"{word}\", \"placeholder\": \"[X]\"}}]}}", ref buffer);
            Take("pdfkit_redact_text", status, ref buffer, Expected("pdfkit_redact_text"));
            status = Native.pdfkit_redact_text(edit, null, ref buffer);
            Take("pdfkit_redact_text", status, ref buffer, Status.InvalidArgument);

            status = Native.pdfkit_redact_images(edit, "{\"treatments\": []}", ref buffer);
            Take("pdfkit_redact_images", status, ref buffer, Expected("pdfkit_redact_images"), Status.InvalidArgument);

            status = Native.pdfkit_set_metadata(edit, "{\"Title\": \"ABI TITLE \\u00e9\"}", ref buffer);
            Take("pdfkit_set_metadata", status, ref buffer, Expected("pdfkit_set_metadata"));
            status = Native.pdfkit_set_metadata(edit, "{\"Bogus\": \"x\"}", ref buffer);
            Take("pdfkit_set_metadata", status, ref buffer, Status.InvalidArgument, Status.NotImplemented);

            status = Native.pdfkit_select_pages(edit, "{\"pages\": [1]}", ref buffer);
            Take("pdfkit_select_pages", status, ref buffer, Status.Ok);
            status = Native.pdfkit_select_pages(edit, "{\"pages\": []}", ref buffer);
            Take("pdfkit_select_pages", status, ref buffer, Status.InvalidArgument);

            var pdf = new PdfkitBuffer();
            status = Native.pdfkit_write(edit, null, ref pdf, ref buffer);
            byte[] output = Bytes("pdfkit_write", ref pdf);
            Take("pdfkit_write", status, ref buffer, Status.Ok, Status.SelfCheck);
            if (status == Status.SelfCheck)
            {
                if (output.Length != 0)
                {
                    throw new AbiFailure(_file, "pdfkit_write", "a refused write handed bytes back");
                }
                return;
            }
            Reopen(output);
        }
        finally
        {
            Native.pdfkit_edit_free(edit);
        }
    }

    private unsafe void Reopen(byte[] output)
    {
        IntPtr written = Open(output, out int status, out string opened);
        if (status != Status.Ok)
        {
            throw new AbiFailure(_file, "pdfkit_open", $"the written output does not open again: {opened}");
        }
        try
        {
            var buffer = new PdfkitBuffer();
            status = Native.pdfkit_metadata(written, null, ref buffer);
            using JsonDocument metadata = Json("pdfkit_metadata", Take("pdfkit_metadata", status, ref buffer, Status.Ok));
            if (_implemented.Contains("pdfkit_set_metadata"))
            {
                JsonElement title = metadata.RootElement.GetProperty("fields")[0];
                if (title.GetProperty("info").GetString() != "ABI TITLE \u00e9")
                {
                    throw new AbiFailure(_file, "pdfkit_metadata", "the written title does not read back");
                }
            }
            fixed (byte* data = output)
            {
                status = Native.pdfkit_check(data, (nuint)output.Length,
                    "{\"strings\": [{\"text\": \"Zebulon Quarterfield\"}]}", ref buffer);
            }
            using JsonDocument check = Json("pdfkit_check", Take("pdfkit_check", status, ref buffer, Status.Ok));
            if (check.RootElement.GetProperty("count").GetInt32() != 0)
            {
                throw new AbiFailure(_file, "pdfkit_check", "the redacted marker survives in the output");
            }
        }
        finally
        {
            Native.pdfkit_close(written);
        }
    }
}

internal static class Program
{
    private static int Main(string[] args)
    {
        if (args.Length != 3 || !uint.TryParse(args[0], out uint expected))
        {
            Console.Error.WriteLine("usage: PdfkitAbi <expected-abi-version> <path-to-pdfkit.dll> <cases.tsv>");
            return 2;
        }
        string dllPath = args[1];
        NativeLibrary.SetDllImportResolver(Assembly.GetExecutingAssembly(),
            (name, assembly, searchPath) => name == "pdfkit" ? NativeLibrary.Load(dllPath) : IntPtr.Zero);
        var session = new Session();
        int files = 0;
        try
        {
            session.Version(expected);
            session.Refusals();
            foreach (string line in System.IO.File.ReadAllLines(args[2], Encoding.UTF8))
            {
                string[] cells = line.Split('\t');
                if (cells.Length != 3)
                {
                    continue;
                }
                session.File(cells[0], cells[1], cells[2]);
                ++files;
            }
        }
        catch (AbiFailure failure)
        {
            Console.Error.WriteLine($"dotnet: FAILED {failure.Message}");
            return 1;
        }
        catch (Exception error) when (error is DllNotFoundException or BadImageFormatException or EntryPointNotFoundException)
        {
            Console.Error.WriteLine($"dotnet: could not load {dllPath}: {error.Message}");
            return 3;
        }
        Console.WriteLine($"dotnet: {files} files, {session.Calls} calls, every expectation held");
        return 0;
    }
}
