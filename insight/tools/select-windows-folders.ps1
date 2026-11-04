# SPDX-License-Identifier: GPL-2.0-or-later
# Loaded only for interactive Windows setup; no folders are created here.
function Test-InsightLocations($Locations) {
    foreach ($name in @('RootDir','MsysRoot','SourceDir','InstallDir')) {
        $value = [string]$Locations[$name]
        if ($value -notmatch '^[A-Za-z]:\\.' -or $value -match '[\r\n]') { throw "${name}: choose an absolute folder below a drive root." }
        $Locations[$name] = [IO.Path]::GetFullPath($value).TrimEnd('\')
    }
    if ($Locations.MsysRoot -notmatch '^[A-Za-z]:\\[A-Za-z0-9_\\.\-]+$') {
        throw 'MSYS2: use a folder path without spaces or non-ASCII characters.'
    }
    $tools = $Locations.MsysRoot.ToLowerInvariant()
    $source = $Locations.SourceDir.ToLowerInvariant()
    $install = $Locations.InstallDir.ToLowerInvariant()
    if ($tools -eq $source -or $tools -eq $install -or $source -eq $install -or
        $source.StartsWith($tools + '\') -or $install.StartsWith($tools + '\') -or
        $tools.StartsWith($source + '\') -or $tools.StartsWith($install + '\') -or
        $source.StartsWith($install + '\')) { throw 'Tools, sources and installation must use separate folders.' }
    return $Locations
}

function New-InsightFolderDialog($Initial) {
    Add-Type -AssemblyName System.Windows.Forms
    Add-Type -AssemblyName System.Drawing
    $ru = (Get-UICulture).Name.StartsWith('ru')
    $texts = if ($ru) {
        @('Geany Insight - выбор папок','Диск','Корневая папка','Инструменты MSYS2','Исходники редактора','Установка редактора','Обзор...','Применить корневую папку','Продолжить','Отмена','Выберите диск и папку. Пути ниже можно менять независимо.')
    } else {
        @('Geany Insight - locations','Drive','Base folder','MSYS2 tools','Editor sources','Editor installation','Browse...','Apply base folder','Continue','Cancel','Choose a drive and base folder. Locations below can be changed independently.')
    }
    [Windows.Forms.Application]::EnableVisualStyles()
    $form = New-Object Windows.Forms.Form
    $form.Name = 'InsightLocations'; $form.Text = $texts[0]
    $form.StartPosition = 'CenterScreen'; $form.AutoScaleMode = 'Dpi'
    $area = [Windows.Forms.Screen]::PrimaryScreen.WorkingArea
    $form.Size = New-Object Drawing.Size([Math]::Min(850, $area.Width - 40), [Math]::Min(340, $area.Height - 40))
    $form.MinimumSize = New-Object Drawing.Size(520, 320)
    $form.MaximizeBox = $false
    $table = New-Object Windows.Forms.TableLayoutPanel
    $table.Dock = 'Fill'; $table.AutoScroll = $true; $table.Padding = New-Object Windows.Forms.Padding(12)
    $table.ColumnCount = 3; $table.RowCount = 9
    $null = $table.ColumnStyles.Add((New-Object Windows.Forms.ColumnStyle('AutoSize')))
    $null = $table.ColumnStyles.Add((New-Object Windows.Forms.ColumnStyle('Percent',100)))
    $null = $table.ColumnStyles.Add((New-Object Windows.Forms.ColumnStyle('AutoSize')))
    $form.Controls.Add($table)
    $intro = New-Object Windows.Forms.Label; $intro.Text=$texts[10]; $intro.AutoSize=$true
    $table.Controls.Add($intro,0,0); $table.SetColumnSpan($intro,3)
    $drive = New-Object Windows.Forms.ComboBox; $drive.Name='Drive'; $drive.DropDownStyle='DropDownList'; $drive.Dock='Fill'
    foreach ($d in @(Get-PSDrive -PSProvider FileSystem | Where-Object { $_.Root -match '^[A-Za-z]:\\$' } | Sort-Object Name)) { $null=$drive.Items.Add($d.Root) }
    $label = New-Object Windows.Forms.Label; $label.Text=$texts[1]; $label.AutoSize=$true
    $table.Controls.Add($label,0,1); $table.Controls.Add($drive,1,1)
    $fields = @{}
    $keys = @('RootDir','MsysRoot','SourceDir','InstallDir')
    for ($i=0; $i -lt $keys.Count; $i++) {
        $key = $keys[$i]; $row=$i+2
        $label=New-Object Windows.Forms.Label; $label.Text=$texts[$i+2]; $label.AutoSize=$true; $label.Anchor='Left'
        $box=New-Object Windows.Forms.TextBox; $box.Name=$key; $box.Text=$Initial[$key]; $box.Dock='Fill'
        $button=New-Object Windows.Forms.Button; $button.Text=$texts[6]; $button.AutoSize=$true; $button.Tag=$box
        $button.Add_Click({
            param($sender, $eventArgs)
            $picker=New-Object Windows.Forms.FolderBrowserDialog
            try {
                $picker.ShowNewFolderButton=$true
                $candidate=$sender.Tag.Text
                while ($candidate -and -not (Test-Path -LiteralPath $candidate -PathType Container)) { $candidate=Split-Path -Parent $candidate }
                if ($candidate) { $picker.SelectedPath=$candidate }
                if ($picker.ShowDialog() -eq [Windows.Forms.DialogResult]::OK) { $sender.Tag.Text=$picker.SelectedPath }
            } finally { $picker.Dispose() }
        })
        $table.Controls.Add($label,0,$row); $table.Controls.Add($box,1,$row); $table.Controls.Add($button,2,$row)
        $fields[$key]=$box
    }
    $apply=New-Object Windows.Forms.Button; $apply.Name='ApplyBase'; $apply.Text=$texts[7]; $apply.AutoSize=$true
    $apply.Add_Click({
        $base=$fields.RootDir.Text.TrimEnd('\')
        $fields.MsysRoot.Text=Join-Path $base 'tools\msys64'
        $fields.SourceDir.Text=Join-Path $base 'src'
        $fields.InstallDir.Text=Join-Path $base 'editor'
    }.GetNewClosure())
    $table.Controls.Add($apply,1,6)
    $rootBox=$fields.RootDir
    $rootBox.Add_TextChanged({ if ($rootBox.Text -match '^[A-Za-z]:\\.') { $apply.PerformClick() } }.GetNewClosure())
    $drive.Add_SelectedIndexChanged({ if ($drive.SelectedItem) { $rootBox.Text=[string]$drive.SelectedItem + $rootBox.Text.Substring([Math]::Min(3,$rootBox.Text.Length)) } }.GetNewClosure())
    if ($drive.Items.Contains($Initial.RootDir.Substring(0,3))) { $drive.SelectedItem=$Initial.RootDir.Substring(0,3) }
    $errorLabel=New-Object Windows.Forms.Label; $errorLabel.Name='LocationError'; $errorLabel.ForeColor=[Drawing.Color]::Firebrick; $errorLabel.AutoSize=$true
    foreach ($field in $fields.Values) { $field.Add_TextChanged({ $errorLabel.Text='' }.GetNewClosure()) }
    $validator=${function:Test-InsightLocations}
    $table.Controls.Add($errorLabel,0,7); $table.SetColumnSpan($errorLabel,3)
    $buttons=New-Object Windows.Forms.FlowLayoutPanel; $buttons.AutoSize=$true; $buttons.Dock='Bottom'; $buttons.FlowDirection='RightToLeft'
    $cancel=New-Object Windows.Forms.Button; $cancel.Text=$texts[9]; $cancel.AutoSize=$true; $cancel.DialogResult='Cancel'
    $ok=New-Object Windows.Forms.Button; $ok.Name='AcceptLocations'; $ok.Text=$texts[8]; $ok.AutoSize=$true
    $ok.Add_Click({
        try {
            $locations=@{}; foreach($key in $fields.Keys) { $locations[$key]=$fields[$key].Text }
            $form.Tag=& $validator $locations
            $form.DialogResult=[Windows.Forms.DialogResult]::OK
            $form.Close()
        } catch { $errorLabel.Text=$_.Exception.Message }
    }.GetNewClosure())
    $buttons.Controls.Add($cancel); $buttons.Controls.Add($ok)
    $table.Controls.Add($buttons,0,8); $table.SetColumnSpan($buttons,3)
    $form.AcceptButton=$ok; $form.CancelButton=$cancel
    return $form
}

function Select-InsightLocations($Initial) {
    if ([Threading.Thread]::CurrentThread.ApartmentState -ne 'STA') { throw 'Folder selection needs STA. Run powershell.exe -STA -File setup-windows.ps1, or use -NonInteractive with explicit paths.' }
    $form=New-InsightFolderDialog $Initial
    try {
        if ($form.ShowDialog() -ne [Windows.Forms.DialogResult]::OK) { throw 'Setup cancelled before downloading or installing anything.' }
        return $form.Tag
    } finally { $form.Dispose() }
}
