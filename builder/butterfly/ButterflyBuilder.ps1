# Butterfly Builder: a window for building Butterfly (Nintendo Switch Sports Resort on PC, built on Longshot) from your own
# copy of the game, and starting it. It runs build_ssr_longshot.bat (beside it) with the choices made here, shows its
# steps as a progress bar, and keeps the choices in settings.txt beside it. Windows PowerShell 5.1 (every Windows 10 and
# 11), nothing to install. Started by "Butterfly Builder.bat", or the desktop shortcut it can make.
# (Plain ASCII on purpose: Windows PowerShell reads a file without a byte order mark in the system's code page.)

Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
[System.Windows.Forms.Application]::EnableVisualStyles()

$Here = Split-Path -Parent $MyInvocation.MyCommand.Path
$BuildScript = Join-Path $Here 'build_ssr_longshot.bat'
$SettingsFile = Join-Path $Here 'settings.txt'
$Logo = Join-Path $Here 'butterfly.png'
$IconFile = Join-Path $Here 'butterfly.ico'

# ---- the colours: Butterfly's (gpu/vk/vk_ui.h), Sports Resort's teal and orange ----------------------------------------
function Rgb($r, $g, $b) { [System.Drawing.Color]::FromArgb($r, $g, $b) }
$Bg = Rgb 6 26 34
$Panel = Rgb 10 36 44
$Cell = Rgb 18 56 66
$Ink = Rgb 234 246 244
$Soft = Rgb 196 222 220
$Dim = Rgb 146 180 180
$Orange = Rgb 246 96 46
$OrangeHi = Rgb 255 140 70
$OrangeInk = Rgb 38 14 4
$Teal = Rgb 48 182 186
$TealInk = Rgb 4 30 36
$Red = Rgb 255 120 100

$FontBody = New-Object System.Drawing.Font('Segoe UI', 10)
$FontStrong = New-Object System.Drawing.Font('Segoe UI Semibold', 10)
$FontTitle = New-Object System.Drawing.Font('Segoe UI Semibold', 20)
$FontStage = New-Object System.Drawing.Font('Segoe UI Semibold', 12)
$FontLog = New-Object System.Drawing.Font('Consolas', 9)

# ---- the settings: key=value lines ---------------------------------------------------------------------------------------
$Settings = @{
    nup     = 'E:\Switch 2 roms\NintendoSwitchSportsResort_v1.1.0.nup'
    base    = 'E:\Butterfly'
    compile = 'all'
}
if (Test-Path $SettingsFile) {
    foreach ($line in Get-Content $SettingsFile) {
        $i = $line.IndexOf('=')
        if ($i -gt 0) { $Settings[$line.Substring(0, $i).Trim()] = $line.Substring($i + 1).Trim() }
    }
}
function Save-Settings {
    $Settings.nup = $NupBox.Text.Trim()
    $Settings.base = $BaseBox.Text.Trim()
    $Settings.compile = $(if ($AllBox.Checked) { 'all' } else { 'most' })
    $lines = foreach ($k in @('nup', 'base', 'compile')) { "$k=$($Settings[$k])" }
    Set-Content -Path $SettingsFile -Value $lines -Encoding ASCII
}
function Game-Folder { Join-Path $BaseBox.Text.Trim() 'game-longshot' }
function Game-Exe { Join-Path (Game-Folder) 'butterfly.exe' }

# ---- controls ------------------------------------------------------------------------------------------------------------
function New-Label($text, $x, $y, $w, $h, $font, $color) {
    $l = New-Object System.Windows.Forms.Label
    $l.Text = $text; $l.Location = New-Object System.Drawing.Point($x, $y); $l.Size = New-Object System.Drawing.Size($w, $h)
    $l.Font = $font; $l.ForeColor = $color; $l.BackColor = [System.Drawing.Color]::Transparent
    return $l
}
function New-Button($text, $x, $y, $w, $h, $back, $fore) {
    $b = New-Object System.Windows.Forms.Button
    $b.Text = $text; $b.Location = New-Object System.Drawing.Point($x, $y); $b.Size = New-Object System.Drawing.Size($w, $h)
    $b.FlatStyle = 'Flat'; $b.FlatAppearance.BorderSize = 0; $b.BackColor = $back; $b.ForeColor = $fore; $b.Font = $FontStrong
    $b.Cursor = [System.Windows.Forms.Cursors]::Hand
    return $b
}
function New-Box($text, $x, $y, $w) {
    $t = New-Object System.Windows.Forms.TextBox
    $t.Text = $text; $t.Location = New-Object System.Drawing.Point($x, $y); $t.Size = New-Object System.Drawing.Size($w, 26)
    $t.BackColor = $Cell; $t.ForeColor = $Ink; $t.BorderStyle = 'FixedSingle'; $t.Font = $FontBody
    return $t
}

$Form = New-Object System.Windows.Forms.Form
$Form.Text = 'Butterfly Builder'
$Form.ClientSize = New-Object System.Drawing.Size(760, 640)
$Form.StartPosition = 'CenterScreen'
$Form.FormBorderStyle = 'FixedSingle'
$Form.MaximizeBox = $false
$Form.BackColor = $Bg
$Form.ForeColor = $Ink
$Form.Font = $FontBody
$Form.AutoScaleMode = 'Dpi'
if (Test-Path $IconFile) { $Form.Icon = New-Object System.Drawing.Icon($IconFile) }

# the header: the logo, the name, what it is
if (Test-Path $Logo) {
    $pic = New-Object System.Windows.Forms.PictureBox
    $pic.Image = [System.Drawing.Image]::FromFile($Logo)
    $pic.SizeMode = 'Zoom'; $pic.Location = New-Object System.Drawing.Point(24, 18); $pic.Size = New-Object System.Drawing.Size(72, 72)
    $pic.BackColor = [System.Drawing.Color]::Transparent
    $Form.Controls.Add($pic)
}
$Form.Controls.Add((New-Label 'Butterfly Builder' 110 22 600 40 $FontTitle $Ink))
$Form.Controls.Add((New-Label 'Nintendo Switch Sports Resort on your PC, built from your own copy of the game. Based on Longshot.' 112 62 620 22 $FontBody $Dim))

# the choices
$Form.Controls.Add((New-Label 'Your game file (.nup)' 24 108 300 20 $FontStrong $Soft))
$NupBox = New-Box $Settings.nup 24 130 600
$Form.Controls.Add($NupBox)
$NupBrowse = New-Button 'Browse...' 636 129 100 28 $Cell $Ink
$Form.Controls.Add($NupBrowse)
$NupBrowse.Add_Click({
    $d = New-Object System.Windows.Forms.OpenFileDialog
    $d.Title = 'Your Nintendo Switch Sports Resort .nup'
    $d.Filter = 'Switch 2 game (*.nup)|*.nup|All files (*.*)|*.*'
    if (Test-Path -LiteralPath $NupBox.Text) { $d.InitialDirectory = Split-Path -Parent $NupBox.Text }
    if ($d.ShowDialog() -eq 'OK') { $NupBox.Text = $d.FileName }
})

$Form.Controls.Add((New-Label 'Butterfly folder (the game, its tools and the build go here)' 24 168 500 20 $FontStrong $Soft))
$BaseBox = New-Box $Settings.base 24 190 600
$Form.Controls.Add($BaseBox)
$BaseBrowse = New-Button 'Browse...' 636 189 100 28 $Cell $Ink
$Form.Controls.Add($BaseBrowse)
$BaseBrowse.Add_Click({
    $d = New-Object System.Windows.Forms.FolderBrowserDialog
    $d.Description = 'The Butterfly folder'
    if (Test-Path -LiteralPath $BaseBox.Text) { $d.SelectedPath = $BaseBox.Text }
    if ($d.ShowDialog() -eq 'OK') { $BaseBox.Text = $d.SelectedPath }
})

$AllBox = New-Object System.Windows.Forms.CheckBox
$AllBox.Text = 'Prepare every shader during the build (a longer build, no stutter when you play)'
$AllBox.Location = New-Object System.Drawing.Point(24, 228); $AllBox.Size = New-Object System.Drawing.Size(700, 24)
$AllBox.Checked = $Settings.compile -ne 'most'; $AllBox.ForeColor = $Ink; $AllBox.BackColor = [System.Drawing.Color]::Transparent
$Form.Controls.Add($AllBox)

# the main buttons
$BuildButton = New-Button 'Build' 24 268 230 48 $Orange $OrangeInk
$BuildButton.Font = $FontStage
$Form.Controls.Add($BuildButton)
$PlayButton = New-Button 'Play' 266 268 230 48 $Teal $TealInk
$PlayButton.Font = $FontStage
$Form.Controls.Add($PlayButton)
$FolderButton = New-Button 'Open folder' 508 268 228 48 $Cell $Ink
$Form.Controls.Add($FolderButton)

# the progress
$StagePanel = New-Object System.Windows.Forms.Panel
$StagePanel.Location = New-Object System.Drawing.Point(24, 332); $StagePanel.Size = New-Object System.Drawing.Size(712, 82)
$StagePanel.BackColor = $Panel
$Form.Controls.Add($StagePanel)
$StageLabel = New-Label 'Ready.' 14 10 520 26 $FontStage $Ink
$StagePanel.Controls.Add($StageLabel)
$EtaLabel = New-Label '' 540 14 160 22 $FontBody $Dim
$EtaLabel.TextAlign = 'TopRight'
$StagePanel.Controls.Add($EtaLabel)
# (a bar of our own: the system's progress bar can't take the colours)
$BarBack = New-Object System.Windows.Forms.Panel
$BarBack.Location = New-Object System.Drawing.Point(14, 46); $BarBack.Size = New-Object System.Drawing.Size(684, 22)
$BarBack.BackColor = $Cell
$StagePanel.Controls.Add($BarBack)
$BarFill = New-Object System.Windows.Forms.Panel
$BarFill.Location = New-Object System.Drawing.Point(0, 0); $BarFill.Size = New-Object System.Drawing.Size(0, 22)
$BarFill.BackColor = $Orange
$BarBack.Controls.Add($BarFill)
function Set-Bar($fraction) {
    if ($fraction -lt 0) { $fraction = 0 }
    if ($fraction -gt 1) { $fraction = 1 }
    $BarFill.Width = [int]($BarBack.Width * $fraction)
}

# what the build says
$LogBox = New-Object System.Windows.Forms.TextBox
$LogBox.Multiline = $true; $LogBox.ReadOnly = $true; $LogBox.ScrollBars = 'Vertical'; $LogBox.WordWrap = $false
$LogBox.Location = New-Object System.Drawing.Point(24, 426); $LogBox.Size = New-Object System.Drawing.Size(712, 150)
$LogBox.BackColor = $Panel; $LogBox.ForeColor = $Soft; $LogBox.BorderStyle = 'None'; $LogBox.Font = $FontLog
$Form.Controls.Add($LogBox)

# the extras
$SavesButton = New-Button 'Import my Lichii save' 24 590 230 34 $Cell $Ink
$Form.Controls.Add($SavesButton)
$ShortcutButton = New-Button 'Desktop shortcut' 266 590 230 34 $Cell $Ink
$Form.Controls.Add($ShortcutButton)
$LogButton = New-Button 'Open the build log' 508 590 228 34 $Cell $Ink
$Form.Controls.Add($LogButton)

function Refresh-Play { $PlayButton.Enabled = (Test-Path -LiteralPath (Game-Exe)) -and -not $script:Building }
function Add-Log($text) {
    $LogBox.AppendText($text + "`r`n")
    if ($LogBox.Lines.Length -gt 800) { $LogBox.Lines = $LogBox.Lines[-600..-1]; $LogBox.SelectionStart = $LogBox.Text.Length; $LogBox.ScrollToCaret() }
}

# ---- the build: build_ssr_longshot.bat with the choices, its output read from a file as it grows -----------------------
$script:Building = $false
$script:Proc = $null
$script:OutFile = $null
$script:OutPos = 0
$script:Partial = ''
$Timer = New-Object System.Windows.Forms.Timer
$Timer.Interval = 400

function Set-Busy($busy) {
    $script:Building = $busy
    $BuildButton.Enabled = -not $busy
    $BuildButton.Text = $(if ($busy) { 'Building...' } else { 'Build' })
    $NupBox.Enabled = -not $busy; $BaseBox.Enabled = -not $busy; $NupBrowse.Enabled = -not $busy; $BaseBrowse.Enabled = -not $busy
    $AllBox.Enabled = -not $busy; $SavesButton.Enabled = -not $busy
    Refresh-Play
}

function Read-Output {
    if (-not $script:OutFile -or -not (Test-Path -LiteralPath $script:OutFile)) { return }
    try {
        $fs = New-Object System.IO.FileStream($script:OutFile, 'Open', 'Read', 'ReadWrite')
        try {
            if ($fs.Length -le $script:OutPos) { return }
            [void]$fs.Seek($script:OutPos, 'Begin')
            $buf = New-Object byte[] ($fs.Length - $script:OutPos)
            $n = $fs.Read($buf, 0, $buf.Length)
            $script:OutPos += $n
            $text = $script:Partial + [System.Text.Encoding]::UTF8.GetString($buf, 0, $n)
        } finally { $fs.Close() }
    } catch { return }
    $lines = $text -split "`r?`n"
    $script:Partial = $lines[-1]
    # (each whole line; the last, unfinished, waits for the rest. A range 0..-1 counts down in PowerShell: none then)
    for ($k = 0; $k -lt $lines.Length - 1; $k++) { Handle-Line $lines[$k] }
}

function Handle-Line($line) {
    if ($line -match '^@@stage (\S+) (.*)$') { $StageLabel.Text = $Matches[2]; Set-Bar 0; $EtaLabel.Text = ''; Add-Log ('-- ' + $Matches[2]); return }
    if ($line -match '^@@progress ([0-9.]+)') { Set-Bar ([double]::Parse($Matches[1], [System.Globalization.CultureInfo]::InvariantCulture)); return }
    if ($line -match '^@@eta (\d+)') {
        $s = [int]$Matches[1]
        $EtaLabel.Text = $(if ($s -ge 60) { 'about ' + [math]::Ceiling($s / 60) + ' min left' } elseif ($s -gt 0) { 'under a minute left' } else { '' })
        return
    }
    if ($line -match '^@@error (.*)$') { Add-Log ('Stopped: ' + $Matches[1]); return }
    if ($line.StartsWith('@@')) { return }   # (@@file, @@plan, @@times, @@done: the bar says enough)
    if ($line.Trim() -eq '') { return }
    # (the steps before the build's own: the script's lines)
    if ($line -match '^(Downloading Butterfly|Copying the compilers|tools: |Building\. )') { $StageLabel.Text = ($line -replace '\s*The log:.*$', '') }
    Add-Log $line
}

$Timer.Add_Tick({
    Read-Output
    if ($script:Proc -and $script:Proc.HasExited) {
        $Timer.Stop()
        Read-Output
        if ($script:Partial) { Handle-Line $script:Partial; $script:Partial = '' }
        $code = $script:Proc.ExitCode
        $script:Proc = $null
        Set-Busy $false
        if ($code -eq 0) {
            $StageLabel.Text = 'Done! Press Play.'; $StageLabel.ForeColor = $Ink; Set-Bar 1; $EtaLabel.Text = ''
        } else {
            $StageLabel.Text = 'The build stopped. Send Claude the end of the build log.'; $StageLabel.ForeColor = $Red
        }
    }
})

$BuildButton.Add_Click({
    $nup = $NupBox.Text.Trim(); $base = $BaseBox.Text.Trim()
    if (-not (Test-Path -LiteralPath $nup)) {
        [System.Windows.Forms.MessageBox]::Show("Can't find your game file:`n$nup", 'Butterfly Builder', 'OK', 'Warning') | Out-Null
        return
    }
    if (-not (Test-Path -LiteralPath $BuildScript)) {
        [System.Windows.Forms.MessageBox]::Show("build_ssr_longshot.bat is missing next to Butterfly Builder:`n$BuildScript", 'Butterfly Builder', 'OK', 'Error') | Out-Null
        return
    }
    if (Get-Process -Name 'butterfly' -ErrorAction SilentlyContinue) {
        [System.Windows.Forms.MessageBox]::Show('Close the game first: the build replaces it.', 'Butterfly Builder', 'OK', 'Information') | Out-Null
        return
    }
    Save-Settings
    if (-not (Test-Path -LiteralPath $base)) { New-Item -ItemType Directory -Path $base | Out-Null }
    $script:OutFile = Join-Path $base 'builder-output.txt'
    Set-Content -Path $script:OutFile -Value '' -Encoding ASCII
    $script:OutPos = 0; $script:Partial = ''
    $LogBox.Clear(); $StageLabel.ForeColor = $Ink; $StageLabel.Text = 'Starting...'; Set-Bar 0; $EtaLabel.Text = ''

    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $env:ComSpec
    $psi.Arguments = '/c ""' + $BuildScript + '" > "' + $script:OutFile + '" 2>&1"'
    $psi.UseShellExecute = $false
    $psi.CreateNoWindow = $true
    $psi.WorkingDirectory = $Here
    $psi.EnvironmentVariables['NUP'] = $nup
    $psi.EnvironmentVariables['BASE'] = $base
    $psi.EnvironmentVariables['COMPILE'] = $(if ($AllBox.Checked) { 'all' } else { 'most' })
    $psi.EnvironmentVariables['BF_PAUSE'] = 'cd .'
    $psi.EnvironmentVariables['PYTHONUNBUFFERED'] = '1'
    $script:Proc = [System.Diagnostics.Process]::Start($psi)
    Set-Busy $true
    $Timer.Start()
})

$PlayButton.Add_Click({
    $exe = Game-Exe
    if (Test-Path -LiteralPath $exe) { Start-Process -FilePath $exe -WorkingDirectory (Game-Folder) }
})
$FolderButton.Add_Click({
    $f = $(if (Test-Path -LiteralPath (Game-Folder)) { Game-Folder } else { $BaseBox.Text.Trim() })
    if (Test-Path -LiteralPath $f) { Start-Process explorer.exe -ArgumentList ('"' + $f + '"') }
})
$LogButton.Add_Click({
    $log = Join-Path $BaseBox.Text.Trim() 'build-longshot.log'
    if (Test-Path -LiteralPath $log) { Start-Process notepad.exe -ArgumentList ('"' + $log + '"') }
    else { [System.Windows.Forms.MessageBox]::Show('There is no build log yet.', 'Butterfly Builder') | Out-Null }
})
$SavesButton.Add_Click({
    $bat = Join-Path $BaseBox.Text.Trim() 'import_lichii_saves.bat'
    if (-not (Test-Path -LiteralPath $bat)) { $bat = Join-Path $Here 'import_lichii_saves.bat' }
    if (Test-Path -LiteralPath $bat) { Start-Process -FilePath $env:ComSpec -ArgumentList ('/c ""' + $bat + '""') }
    else { [System.Windows.Forms.MessageBox]::Show('import_lichii_saves.bat was not found.', 'Butterfly Builder') | Out-Null }
})
$ShortcutButton.Add_Click({
    $desktop = [Environment]::GetFolderPath('Desktop')
    $shell = New-Object -ComObject WScript.Shell
    $link = $shell.CreateShortcut((Join-Path $desktop 'Butterfly Builder.lnk'))
    $link.TargetPath = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
    $link.Arguments = '-NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File "' + (Join-Path $Here 'ButterflyBuilder.ps1') + '"'
    $link.WorkingDirectory = $Here
    if (Test-Path $IconFile) { $link.IconLocation = $IconFile }
    $link.Description = 'Build and play Butterfly'
    $link.Save()
    $said = 'A Butterfly Builder shortcut is on your desktop.'
    if (Test-Path -LiteralPath (Game-Exe)) {
        $game = $shell.CreateShortcut((Join-Path $desktop 'Butterfly.lnk'))
        $game.TargetPath = Game-Exe
        $game.WorkingDirectory = Game-Folder
        $game.Description = 'Nintendo Switch Sports Resort on PC'
        $game.Save()
        $said = 'Two shortcuts are on your desktop: Butterfly (the game) and Butterfly Builder.'
    }
    [System.Windows.Forms.MessageBox]::Show($said, 'Butterfly Builder') | Out-Null
})

$Form.Add_FormClosing({
    param($sender, $e)
    if ($script:Building -and $script:Proc -and -not $script:Proc.HasExited) {
        $answer = [System.Windows.Forms.MessageBox]::Show('A build is running. Stop it and close?', 'Butterfly Builder', 'YesNo', 'Question')
        if ($answer -ne 'Yes') { $e.Cancel = $true; return }
        Start-Process -FilePath 'taskkill.exe' -ArgumentList ('/T /F /PID ' + $script:Proc.Id) -WindowStyle Hidden -Wait
    }
    Save-Settings
})

Set-Busy $false
if (Test-Path -LiteralPath (Game-Exe)) { $StageLabel.Text = 'Ready. Press Play, or Build to update.' }
else { $StageLabel.Text = 'Pick your game file, then press Build.' }
[void]$Form.ShowDialog()
