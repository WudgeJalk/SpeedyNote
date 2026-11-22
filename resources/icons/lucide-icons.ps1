# Replaces the app's PNG icons with Lucide icons (https://lucide.dev, ISC license).
# For each app icon name: download the Lucide SVG, then render name.png (black,
# for the light theme) and name_reversed.png (white, for the dark theme) at 256 px.
# Usage: lucide-icons.ps1 -Repo <repo root> -Work <empty work dir> [-Only name,name]
param([string]$Repo, [string]$Work, [string[]]$Only)

$map = [ordered]@{
    addtab = 'plus'; aligncenter = 'text-align-center|align-center'; alignleft = 'text-align-start|align-left'
    alignright = 'text-align-end|align-right'; auto = 'type'; bookmark = 'bookmark'; bookpage = 'book-open'
    borders = 'square'; check = 'check'; clipboard = 'clipboard'; copy = 'copy'; cross = 'x'; cut = 'scissors'
    down_arrow = 'chevron-down'; edit = 'square-pen'; eraser = 'eraser'; eraser_partial = 'scissors-line-dashed'
    export = 'share'; fastforward = 'fast-forward'; folder = 'folder'; fullscreen = 'maximize'; hand = 'hand'
    grip = 'grip-vertical'; grip_horizontal = 'grip-horizontal'; laser = 'presentation'; ruler = 'ruler'
    highlight_dotted = 'ellipsis'; highlight_none = 'square-dashed-text'; highlight_underline = 'underline'
    ibeam = 'text-cursor-input'; import = 'import'; layer = 'layers'; layer_downarrow = 'arrow-down'
    layer_uparrow = 'arrow-up'; left_arrow = 'arrow-left'; leftsidebar = 'panel-left'; link = 'link'
    link_quote = 'message-square-quote'; linkicon = 'quote'; lock = 'lock'; markdown = 'notebook-pen'
    marker = 'highlighter'; menu = 'ellipsis-vertical'; move = 'move'; notvisible = 'eye-off'
    objectinsert = 'image'; ocr = 'scan-text'; outline = 'list-tree'; paste = 'clipboard-paste'
    pdf = 'file-text'; pen = 'pen'; recent = 'rotate-ccw-clock'; redo = 'redo'; right_arrow = 'arrow-right'
    rightsidebar = 'panel-right'; rope = 'lasso'; rope_partial = 'lasso-select'; save = 'save'; exit_fullscreen = 'minimize'; win_restore = 'copy'; win_max = 'square'; win_min = 'minus'; home = 'house'; search = 'search'
    scan = 'scan'; scanall = 'scan-line'; scroll = 'move-vertical'; select = 'square-dashed-mouse-pointer|mouse-pointer-2'
    settings = 'settings'; shape = 'shapes'; showtext = 'text-initial'; star = 'star'; straightLine = 'slash'
    tabs = 'app-window'; text = 'text-cursor'; thickness = 'spline'; trash = 'trash'; undo = 'undo'
    up_arrow = 'chevron-up'; url = 'link-2'; visible = 'eye'; warning = 'triangle-exclamation-point'; zoom = 'zoom-in'
    pen_fountain = 'pen-tool'; brush = 'brush'; editpoints = 'vector-square'; corners = 'square-round-corner|squircle'; saving = 'loader-circle|loader'
}
# Turned so the tip is at the bottom left, where the toolbar paints the tool's colour.
$rotate = @{ pen_fountain = -90 }

$svgDir = Join-Path $Work 'svg'; New-Item -ItemType Directory -Force $svgDir | Out-Null
$icons = Join-Path $Repo 'resources\icons'
$rsvg = 'C:\msys64\clang64\bin\rsvg-convert.exe'
$done = 0; $failed = @()

foreach ($name in $map.Keys) {
    if ($Only -and $name -notin $Only) { continue }
    if (-not (Test-Path (Join-Path $icons "$name.png"))) { $failed += "$name (no such app icon)"; continue }
    $svg = $null
    foreach ($candidate in $map[$name].Split('|')) {
        $file = Join-Path $svgDir "$candidate.svg"
        if (-not (Test-Path $file)) {
            curl.exe -sSLf -A 'curl' -o $file "https://raw.githubusercontent.com/lucide-icons/lucide/main/icons/$candidate.svg" 2>$null
        }
        if ((Test-Path $file) -and (Select-String -Path $file -Pattern '<svg' -Quiet)) { $svg = $file; break }
        Remove-Item $file -ErrorAction SilentlyContinue
    }
    if (-not $svg) { $failed += "$name ($($map[$name]) not found)"; continue }

    $text = Get-Content $svg -Raw
    if ($rotate.Contains($name)) {
        $text = ($text -replace '(<svg[^>]*>)', "`$1<g transform=`"rotate($($rotate[$name]) 12 12)`">") -replace '</svg>', '</g></svg>'
    }
    foreach ($variant in @(@{ suffix = ''; color = '#000000' }, @{ suffix = '_reversed'; color = '#ffffff' })) {
        $colored = Join-Path $Work "tmp.svg"
        [System.IO.File]::WriteAllText($colored, $text.Replace('currentColor', $variant.color))
        $out = Join-Path $icons "$name$($variant.suffix).png"
        if (-not (Test-Path $out)) { continue }   # Only replace files the app already has
        & $rsvg -w 256 -h 256 -o $out $colored
        if ($LASTEXITCODE -ne 0) { $failed += "$name$($variant.suffix) (render failed)" }
    }
    $done++
}
"replaced: $done of $($map.Count)"
"failed: $($failed -join '; ')"
