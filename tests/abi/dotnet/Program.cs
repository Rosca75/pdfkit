// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The .NET side of the ABI tier. It loads pdfkit.dll from the path given,
// calls pdfkit_abi_version, pdfkit_version and pdfkit_buffer_free through
// P/Invoke, prints the version JSON, and exits 0 only when the ABI version
// equals the one expected. Usage: AbiHello <expected-abi-version> <path-to-pdfkit.dll>.

using System;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Text;

namespace Pdfkit.AbiHello;

// The pdfkit_buffer struct of include/pdfkit/pdfkit.h.
[StructLayout(LayoutKind.Sequential)]
internal struct PdfkitBuffer
{
    public IntPtr Data;
    public UIntPtr Size;
    public IntPtr Reserved;
}

internal static class Native
{
    private const string Library = "pdfkit";

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern uint pdfkit_abi_version();

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int pdfkit_version(ref PdfkitBuffer output);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void pdfkit_buffer_free(ref PdfkitBuffer buffer);
}

internal static class Program
{
    private static int Main(string[] args)
    {
        if (args.Length != 2 || !uint.TryParse(args[0], out uint expected))
        {
            Console.Error.WriteLine("usage: AbiHello <expected-abi-version> <path-to-pdfkit.dll>");
            return 2;
        }
        string dllPath = args[1];
        NativeLibrary.SetDllImportResolver(Assembly.GetExecutingAssembly(),
            (name, assembly, searchPath) => name == "pdfkit" ? NativeLibrary.Load(dllPath) : IntPtr.Zero);
        try
        {
            uint abi = Native.pdfkit_abi_version();
            var buffer = new PdfkitBuffer();
            int status = Native.pdfkit_version(ref buffer);
            string json = Marshal.PtrToStringUTF8(buffer.Data, checked((int)buffer.Size.ToUInt64()));
            Native.pdfkit_buffer_free(ref buffer);
            Console.WriteLine(json);
            Console.WriteLine($"dotnet: pdfkit_abi_version() = {abi}, pdfkit_version status = {status}");
            if (status != 0 || abi != expected || buffer.Data != IntPtr.Zero)
            {
                Console.Error.WriteLine($"dotnet: expected ABI version {expected}, status 0 and a freed buffer");
                return 1;
            }
            return 0;
        }
        catch (Exception error) when (error is DllNotFoundException or BadImageFormatException or EntryPointNotFoundException)
        {
            Console.Error.WriteLine($"dotnet: could not load {dllPath}: {error.Message}");
            return 3;
        }
    }
}
