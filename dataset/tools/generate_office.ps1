# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# Writes one reference document through every PDF generator on the owner's
# laptop and registers the outputs in the dataset: Word (Save as PDF, Save as
# PDF/A, Microsoft Print to PDF), Excel and PowerPoint (Save as PDF and Microsoft
# Print to PDF) and Edge headless (print to PDF of an HTML version). The text is
# held in this script and names invented people only. Every step runs in its own
# PowerShell child with 120 s to finish; after it, every process the step started
# (the Office server started for automation, the Edge instance on its own
# profile) is killed with its tree whatever state it is in, so a dialog or a hang
# never blocks the night run. Processes the owner had open are never touched.
#
# Sources (recalled, the outcome of each call is recorded in generation.json):
# Word Document.ExportAsFixedFormat (wdExportFormatPDF = 17, UseISO19005_1 for
# PDF/A) and Document.PrintOut with OutputFileName; Excel Workbook.
# ExportAsFixedFormat (xlTypePDF = 0) and Workbook.PrintOut with PrToFileName;
# PowerPoint Presentation.SaveAs (ppSaveAsPDF = 32) and PrintOut with
# PrintToFile; Edge's --headless and --print-to-pdf switches.

param(
    [string]$Step = "all",
    [string]$OutDir = "",
    [int]$TimeoutSeconds = 120,
    [switch]$NoRegister,
    [string]$Only = "word,excel,powerpoint,edge"
)

$ErrorActionPreference = "Stop"
$Printer = "Microsoft Print to PDF"

$Prose = @(
    "Invoice 2026-114 for Jane Doe, Example Street 12, Luxembourg.",
    "Payment is due to the account IBAN LU28 0019 4006 4475 0000 held by Carl Sample.",
    "The office team of the Fictional Holding company reviewed the affine transformation of every flight schedule and found the internationalisation of the documentation satisfactory.",
    "Maria Testing confirmed the reconciliation of the quarterly characteristics and the extraordinarily comprehensive standardisation report."
)
$Columns = @(
    "The first column describes the redaction policy: every name and every account number is replaced by a placeholder of the same width.",
    "The second column describes the extraction: every glyph is read with its position, so a reader can find each name again."
)
$TableRows = @(
    @("Name", "Account", "Amount"),
    @("Jane Doe", "LU28 0019 4006", "1,200.00"),
    @("Carl Sample", "LU12 3456 7890", "350.00"),
    @("Maria Testing", "LU99 0001 0002", "42.00")
)
$HeaderText = "Fictional Holding - confidential"
$FooterText = "Reference document for the pdfkit dataset"

function Resolve-Root {
    if ($env:PDFKIT_DATASET_ROOT) { return $env:PDFKIT_DATASET_ROOT }
    $common = & git -C $PSScriptRoot rev-parse --git-common-dir
    $commonPath = Resolve-Path (Join-Path $PSScriptRoot $common) -ErrorAction SilentlyContinue
    if (-not $commonPath) { $commonPath = Resolve-Path $common }
    return (Join-Path (Split-Path $commonPath -Parent) "dataset")
}

function New-Picture([string]$Path) {
    Add-Type -AssemblyName System.Drawing
    $bitmap = New-Object System.Drawing.Bitmap 160, 90
    for ($x = 0; $x -lt 160; $x++) {
        for ($y = 0; $y -lt 90; $y++) {
            $bitmap.SetPixel($x, $y, [System.Drawing.Color]::FromArgb(255, ($x * 3) % 256, ($y * 5) % 256, 128))
        }
    }
    $bitmap.Save($Path, [System.Drawing.Imaging.ImageFormat]::Png)
    $bitmap.Dispose()
}

function Get-OwnedProcesses([string]$Marker) {
    # The processes a step started: Office servers started for automation, or Edge on our profile.
    Get-CimInstance Win32_Process | Where-Object {
        ($_.Name -in @("WINWORD.EXE", "EXCEL.EXE", "POWERPNT.EXE") -and $_.CommandLine -match "Embedding|/Automation") -or
        ($_.Name -eq "msedge.exe" -and $_.CommandLine -like "*$Marker*")
    }
}

function Stop-Tree([int]$ProcessId) {
    # Kills a process and its descendants; a process that is already gone is not an error.
    foreach ($kid in @(Get-CimInstance Win32_Process -Filter "ParentProcessId=$ProcessId" -ErrorAction SilentlyContinue)) {
        Stop-Tree $kid.ProcessId
    }
    Stop-Process -Id $ProcessId -Force -ErrorAction SilentlyContinue
}

function Invoke-Step([string]$Name, [string]$Out) {
    $before = @(Get-OwnedProcesses "pdfkit-edge-profile" | ForEach-Object { $_.ProcessId })
    $childArgs = @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", $PSCommandPath, "-Step", $Name, "-OutDir", $Out)
    $child = Start-Process -FilePath "powershell.exe" -ArgumentList $childArgs -PassThru -WindowStyle Hidden
    $finished = $child.WaitForExit($TimeoutSeconds * 1000)
    $after = @(Get-OwnedProcesses "pdfkit-edge-profile" | Where-Object { $before -notcontains $_.ProcessId })
    foreach ($proc in $after) { Stop-Tree $proc.ProcessId }
    if (-not $finished) { Stop-Tree $child.Id }
    return @{ step = $Name; finished = $finished; killed = $after.Count }
}

function Wait-File([string]$Path, [int]$Seconds = 60) {
    # A print job reaches the file through the spooler after the call returns.
    $last = -1
    for ($i = 0; $i -lt $Seconds; $i++) {
        if (Test-Path $Path) {
            $size = (Get-Item $Path).Length
            if ($size -gt 0 -and $size -eq $last) { return "ok" }
            $last = $size
        }
        Start-Sleep -Seconds 1
    }
    return "missing"
}

function Write-Word([string]$Out, [string]$Picture) {
    $word = New-Object -ComObject Word.Application
    $word.Visible = $false
    $word.DisplayAlerts = 0
    $result = @{ version = $word.Version; build = $word.Build }
    try {
        $doc = $word.Documents.Add()
        $doc.AutoHyphenation = $true
        $range = $doc.Content
        $range.Text = ""
        $sel = $word.Selection
        $sel.Font.Bold = $true
        $sel.TypeText("Reference document")
        $sel.Font.Bold = $false
        $sel.TypeParagraph()
        foreach ($p in $Prose) { $sel.TypeText($p); $sel.TypeParagraph() }
        $sel.Font.Italic = $true
        $sel.TypeText("This sentence is set in italic type.")
        $sel.Font.Italic = $false
        $sel.TypeParagraph()
        $sel.InlineShapes.AddPicture($Picture) | Out-Null
        $sel.TypeParagraph()
        $sel.InsertBreak(3)
        foreach ($c in $Columns) { $sel.TypeText($c); $sel.TypeParagraph() }
        $doc.Sections.Item(2).PageSetup.TextColumns.SetCount(2)
        $sel.InsertBreak(3)
        $doc.Sections.Item(3).PageSetup.TextColumns.SetCount(1)
        $table = $doc.Tables.Add($sel.Range, $TableRows.Count, 3)
        $table.Borders.Enable = 1
        for ($r = 0; $r -lt $TableRows.Count; $r++) {
            for ($c = 0; $c -lt 3; $c++) { $table.Cell($r + 1, $c + 1).Range.Text = $TableRows[$r][$c] }
        }
        $doc.Sections.Item(1).Headers.Item(1).Range.Text = $HeaderText
        $doc.Sections.Item(1).Footers.Item(1).Range.Text = $FooterText
        $doc.Sections.Item(1).Footers.Item(1).PageNumbers.Add(2) | Out-Null
        $docx = Join-Path $Out "reference.docx"
        $doc.SaveAs2($docx, 16)
        $doc.ExportAsFixedFormat((Join-Path $Out "word_save.pdf"), 17, $false, 0, 0, 1, 1, 0, $true, $true, 1, $true, $true, $false)
        $result.word_save = "ok"
        $doc.ExportAsFixedFormat((Join-Path $Out "word_pdfa.pdf"), 17, $false, 0, 0, 1, 1, 0, $true, $true, 1, $true, $true, $true)
        $result.word_pdfa = "ok"
        $previous = $word.ActivePrinter
        try {
            $word.ActivePrinter = $Printer
            $doc.PrintOut($false, $false, 0, (Join-Path $Out "word_print.pdf"), "", "", 0, 1, "", 0, $true)
            $result.word_print = Wait-File (Join-Path $Out "word_print.pdf")
        } finally {
            $word.ActivePrinter = $previous
        }
        $doc.Close(0)
    } finally {
        $word.Quit(0)
    }
    return $result
}

function Write-Excel([string]$Out) {
    $excel = New-Object -ComObject Excel.Application
    $excel.Visible = $false
    $excel.DisplayAlerts = $false
    $result = @{ version = $excel.Version }
    try {
        $book = $excel.Workbooks.Add()
        $sheet = $book.Worksheets.Item(1)
        $sheet.Cells.Item(1, 1).Value2 = "Reference workbook"
        for ($r = 0; $r -lt $TableRows.Count; $r++) {
            for ($c = 0; $c -lt 3; $c++) { $sheet.Cells.Item($r + 3, $c + 1).Value2 = $TableRows[$r][$c] }
        }
        $sheet.Range("A3:C6").Borders.LineStyle = 1
        for ($i = 0; $i -lt $Prose.Count; $i++) { $sheet.Cells.Item($i + 9, 1).Value2 = $Prose[$i] }
        $sheet.Columns.Item(1).ColumnWidth = 30
        $sheet.PageSetup.CenterHeader = $HeaderText
        $sheet.PageSetup.CenterFooter = $FooterText
        $book.ExportAsFixedFormat(0, (Join-Path $Out "excel_save.pdf"))
        $result.excel_save = "ok"
        $missing = [Type]::Missing
        $book.PrintOut($missing, $missing, 1, $false, $Printer, $true, $true, (Join-Path $Out "excel_print.pdf"))
        $result.excel_print = Wait-File (Join-Path $Out "excel_print.pdf")
        $book.Close($false)
    } finally {
        $excel.Quit()
    }
    return $result
}

function Write-PowerPoint([string]$Out) {
    $ppt = New-Object -ComObject PowerPoint.Application
    $result = @{ version = $ppt.Version }
    try {
        $pres = $ppt.Presentations.Add(0)
        $slide = $pres.Slides.Add(1, 2)
        $slide.Shapes.Item(1).TextFrame.TextRange.Text = "Reference deck"
        $slide.Shapes.Item(2).TextFrame.TextRange.Text = ($Prose -join "`r")
        $slide2 = $pres.Slides.Add(2, 12)
        $shape = $slide2.Shapes.AddTable($TableRows.Count, 3, 50, 80, 600, 200)
        for ($r = 0; $r -lt $TableRows.Count; $r++) {
            for ($c = 0; $c -lt 3; $c++) {
                $shape.Table.Cell($r + 1, $c + 1).Shape.TextFrame.TextRange.Text = $TableRows[$r][$c]
            }
        }
        $pres.SaveAs((Join-Path $Out "powerpoint_save.pdf"), 32)
        $result.powerpoint_save = "ok"
        $pres.PrintOptions.ActivePrinter = $Printer
        $pres.PrintOut(1, 2, (Join-Path $Out "powerpoint_print.pdf"))
        $result.powerpoint_print = Wait-File (Join-Path $Out "powerpoint_print.pdf")
        $pres.Close()
    } finally {
        $ppt.Quit()
    }
    return $result
}

function Write-Edge([string]$Out, [string]$Picture) {
    $edge = "${env:ProgramFiles(x86)}\Microsoft\Edge\Application\msedge.exe"
    if (-not (Test-Path $edge)) { $edge = "$env:ProgramFiles\Microsoft\Edge\Application\msedge.exe" }
    $html = Join-Path $Out "reference.html"
    $rows = ($TableRows | ForEach-Object { "<tr><td>" + ($_ -join "</td><td>") + "</td></tr>" }) -join "`n"
    $body = @"
<!doctype html><html><head><meta charset="utf-8"><title>Reference document</title>
<style>body{font-family:Arial,sans-serif;hyphens:auto} .cols{column-count:2} table,td{border:1px solid #000;border-collapse:collapse}</style>
</head><body lang="en"><header>$HeaderText</header><h1>Reference document</h1>
$(($Prose | ForEach-Object { "<p>$_</p>" }) -join "`n")
<p><b>Bold</b> and <i>italic</i> words.</p><img src="reference.png" width="160" height="90">
<div class="cols">$(($Columns | ForEach-Object { "<p>$_</p>" }) -join "`n")</div>
<table>$rows</table><footer>$FooterText</footer></body></html>
"@
    Set-Content -Path $html -Value $body -Encoding UTF8
    $profile = Join-Path $Out "pdfkit-edge-profile"
    $pdf = Join-Path $Out "edge_print.pdf"
    $uri = ([System.Uri]$html).AbsoluteUri
    $proc = Start-Process -FilePath $edge -ArgumentList @("--headless", "--disable-gpu", "--no-first-run",
        "--user-data-dir=`"$profile`"", "--print-to-pdf=`"$pdf`"", $uri) -PassThru
    $proc.WaitForExit(90000) | Out-Null
    $version = (Get-Item $edge).VersionInfo.ProductVersion
    return @{ version = $version; edge_print = $(if (Test-Path $pdf) { "ok" } else { "missing" }) }
}

$Generators = @{
    "word_save"        = @{ tags = "gen_word,header_footer,multi_column,table"; drop = "" }
    "word_pdfa"        = @{ tags = "gen_word_pdfa,gen_word,header_footer,multi_column,table"; drop = "" }
    "word_print"       = @{ tags = "gen_print_to_pdf,header_footer,multi_column,table"; drop = "gen_word" }
    "excel_save"       = @{ tags = "gen_excel,table,header_footer"; drop = "" }
    "excel_print"      = @{ tags = "gen_print_to_pdf,table,header_footer"; drop = "gen_excel" }
    "powerpoint_save"  = @{ tags = "gen_powerpoint,table"; drop = "" }
    "powerpoint_print" = @{ tags = "gen_print_to_pdf,table"; drop = "gen_powerpoint" }
    "edge_print"       = @{ tags = "gen_edge,table,multi_column"; drop = "gen_chrome" }
}

if ($Step -ne "all") {
    $log = Join-Path $OutDir "$Step.json"
    try {
        $picture = Join-Path $OutDir "reference.png"
        switch ($Step) {
            "word" { $r = Write-Word $OutDir $picture }
            "excel" { $r = Write-Excel $OutDir }
            "powerpoint" { $r = Write-PowerPoint $OutDir }
            "edge" { $r = Write-Edge $OutDir $picture }
        }
        $r.outcome = "ok"
    } catch {
        $r = @{ outcome = "failed"; error = $_.Exception.Message }
    }
    $r | ConvertTo-Json | Set-Content -Path $log -Encoding UTF8
    exit 0
}

$root = Resolve-Root
if (-not $OutDir) { $OutDir = Join-Path $root "cache\office-staging" }
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
foreach ($old in Get-ChildItem $OutDir -Filter *.pdf -ErrorAction SilentlyContinue) {
    if (($Only -split ",") | Where-Object { $old.Name -like "$_*" }) { Remove-Item $old.FullName -Force }
}
New-Picture (Join-Path $OutDir "reference.png")
$defaultPrinter = (Get-CimInstance Win32_Printer | Where-Object { $_.Default }).Name

$summary = @{ generated = @{}; steps = @() }
foreach ($name in @("word", "excel", "powerpoint", "edge") | Where-Object { ($Only -split ",") -contains $_ }) {
    $summary.steps += Invoke-Step $name $OutDir
    $logPath = Join-Path $OutDir "$name.json"
    if (Test-Path $logPath) { $summary.generated[$name] = Get-Content $logPath -Raw | ConvertFrom-Json }
}

$nowDefault = (Get-CimInstance Win32_Printer | Where-Object { $_.Default }).Name
if ($defaultPrinter -and $nowDefault -ne $defaultPrinter) {
    $printer = Get-CimInstance Win32_Printer | Where-Object { $_.Name -eq $defaultPrinter }
    Invoke-CimMethod -InputObject $printer -MethodName SetDefaultPrinter | Out-Null
    $summary.default_printer_restored = $defaultPrinter
}

$registered = @()
if (-not $NoRegister) {
    $add = Join-Path $PSScriptRoot "add.py"
    foreach ($name in $Generators.Keys | Sort-Object) {
        $pdf = Join-Path $OutDir "$name.pdf"
        if (-not (Test-Path $pdf)) { $registered += @{ generator = $name; status = "absent" }; continue }
        $g = $Generators[$name]
        $argsList = @($add, $pdf, "--set", "office", "--licence", "Apache-2.0", "--source", "office:$name",
            "--tags", $g.tags, "--notes", "generated by generate_office.ps1")
        if ($g.drop) { $argsList += @("--drop-tags", $g.drop) }
        & python @argsList | Out-Null
        $registered += @{ generator = $name; status = $(if ($LASTEXITCODE -eq 0) { "registered" } else { "failed" }) }
    }
}
$summary.registered = $registered
$summary | ConvertTo-Json -Depth 5 | Tee-Object -FilePath (Join-Path $OutDir "generation.json")
