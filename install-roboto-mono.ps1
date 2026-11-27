# install-roboto-mono.ps1
#
# Downloads the Roboto Mono font family and installs it for the
# current user only. No administrator rights are needed.
#
# Called by install-xml2rfc.bat (it must sit in the same folder as
# that .bat). You can also run it on its own from PowerShell:
#   powershell -ExecutionPolicy Bypass -File install-roboto-mono.ps1
#
# What it does:
#   1. Downloads the Roboto Mono variable fonts (upright + italic)
#      from the google/fonts repository on GitHub. These two files
#      are the whole family: a variable font covers every weight
#      from Thin to Bold, so there is no separate file per weight.
#      (The zip you get from the Google Fonts website also has a
#      'static' folder of per-weight files; those are the same
#      family, just split up. Installing the two variable fonts
#      gives Windows the same "Roboto Mono" family.)
#   2. Copies every .ttf (and .otf) it finds in the download folder
#      -- top folder and every subfolder -- into the per-user fonts
#      folder (%LOCALAPPDATA%\Microsoft\Windows\Fonts). So if you
#      drop extra font files into the download folder before
#      running this, they get installed too.
#   3. Registers each font for the current user (HKCU) so Windows
#      lists it, and tells Windows the font list changed
#      (WM_FONTCHANGE) so running programs can see it.
#
# Exit code: 0 if at least one font was installed, 1 if anything
# failed (download failed, a file was not a real font, nothing
# installed). The caller (install-xml2rfc.bat) prints the result.

$ErrorActionPreference = 'Stop'

# The two variable fonts that make up the Roboto Mono family.
# Each entry gives the download URL and the file name to save it
# under. The file name MUST NOT contain square brackets: the name
# in the URL is RobotoMono[wght].ttf, and PowerShell treats [ and ]
# as wildcard characters, so saving under that name makes
# Invoke-WebRequest fail with "Unable to find the specified file."
# We save under a plain name instead; the name Windows shows for
# the font comes from the registry display name below, not from
# the file name, so this changes nothing the user sees.
$fonts = @(
    @{ Url = 'https://raw.githubusercontent.com/google/fonts/main/ofl/robotomono/RobotoMono%5Bwght%5D.ttf';        File = 'RobotoMono-Variable.ttf';        Display = 'Roboto Mono (TrueType)' }
    @{ Url = 'https://raw.githubusercontent.com/google/fonts/main/ofl/robotomono/RobotoMono-Italic%5Bwght%5D.ttf'; File = 'RobotoMono-Italic-Variable.ttf'; Display = 'Roboto Mono Italic (TrueType)' }
)

# Pretty names for the registry (what the font is called in
# Windows' font list), keyed by the file name we saved under.
# Anything else keeps its file name.
$displayNames = @{
    'RobotoMono-Variable.ttf'        = 'Roboto Mono (TrueType)'
    'RobotoMono-Italic-Variable.ttf' = 'Roboto Mono Italic (TrueType)'
}

$downloadDir = Join-Path $env:TEMP 'RobotoMono'
$fontsFolder = Join-Path $env:LOCALAPPDATA 'Microsoft\Windows\Fonts'
$regPath     = 'HKCU:\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Fonts'

function Test-IsFontFile {
    # A real .ttf starts with 00 01 00 00; a real .otf starts with
    # the letters OTTO. Anything else (for example an HTML error
    # page from a failed download) is not a font.
    param([string]$Path)
    $bytes = [System.IO.File]::ReadAllBytes($Path)
    if ($bytes.Length -lt 10000) { return $false }
    if ($bytes[0] -eq 0 -and $bytes[1] -eq 1 -and $bytes[2] -eq 0 -and $bytes[3] -eq 0) { return $true }
    if ($bytes[0] -eq 0x4F -and $bytes[1] -eq 0x54 -and $bytes[2] -eq 0x54 -and $bytes[3] -eq 0x4F) { return $true }
    return $false
}

try {
    New-Item -ItemType Directory -Force -Path $downloadDir | Out-Null
    New-Item -ItemType Directory -Force -Path $fontsFolder | Out-Null

    # --- Download ---
    foreach ($font in $fonts) {
        $fileName = $font.File
        $url = $font.Url
        $dest = Join-Path $downloadDir $fileName
        Write-Host "  Downloading $fileName ..."
        Invoke-WebRequest -Uri $url -OutFile $dest
        if (-not (Test-IsFontFile -Path $dest)) {
            Write-Host "  ERROR: $fileName did not download as a font file."
            Write-Host "  (It may be an error page. Check you are online and try again.)"
            exit 1
        }
    }

    # --- Install every font file found (top folder + subfolders) ---
    $found = Get-ChildItem -Path $downloadDir -Recurse -Include *.ttf, *.otf -File
    if ($found.Count -eq 0) {
        Write-Host '  ERROR: no font files were found to install.'
        exit 1
    }
    $installed = 0
    foreach ($font in $found) {
        $target = Join-Path $fontsFolder $font.Name
        Copy-Item -LiteralPath $font.FullName -Destination $target -Force
        if ($displayNames.ContainsKey($font.Name)) {
            $displayName = $displayNames[$font.Name]
        }
        else {
            $displayName = ([System.IO.Path]::GetFileNameWithoutExtension($font.Name)) + ' (TrueType)'
        }
        New-ItemProperty -Path $regPath -Name $displayName -Value $target -PropertyType String -Force | Out-Null
        Write-Host "  Installed: $($font.Name)"
        $installed++
    }

    # --- Tell Windows the font list changed ---
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class FontChangeNotifier {
    [DllImport("user32.dll", SetLastError = true)]
    public static extern IntPtr SendMessageTimeout(
        IntPtr hWnd, uint Msg, UIntPtr wParam, string lParam,
        uint fuFlags, uint uTimeout, out UIntPtr lpdwResult);
}
'@
    $HWND_BROADCAST = [IntPtr]0xffff
    $WM_FONTCHANGE  = 0x001D
    $result = [UIntPtr]::Zero
    [FontChangeNotifier]::SendMessageTimeout(
        $HWND_BROADCAST, $WM_FONTCHANGE, [UIntPtr]::Zero, $null,
        0, 1000, [ref]$result) | Out-Null

    Write-Host "  Done: installed $installed font file(s) for the current user."
    Write-Host '  (If a program that was already open does not offer Roboto Mono,'
    Write-Host '   close and reopen that program. A restart is almost never needed.)'
    exit 0
}
catch {
    Write-Host "  ERROR: $($_.Exception.Message)"
    exit 1
}
