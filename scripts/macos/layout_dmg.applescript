on run argv
    set mountFolder to POSIX file (item 1 of argv) as alias
    tell application "Finder"
        tell folder mountFolder
            open
            set current view of container window to icon view
            set toolbar visible of container window to false
            set statusbar visible of container window to false
            set sidebar width of container window to 0
            set bounds of container window to {120, 120, 760, 572}
            set viewOptions to icon view options of container window
            set arrangement of viewOptions to not arranged
            set icon size of viewOptions to 80
            set text size of viewOptions to 13
            set label position of viewOptions to bottom
            set shows item info of viewOptions to false
            set background picture of viewOptions to file ".background:background.png"
            set position of item "UpgradeTool-GUI.app" to {160, 130}
            set position of item "Applications" to {480, 130}
            set position of item "config.ini" to {320, 320}
            update without registering applications
            delay 2
            close
            open
            delay 2
            close
        end tell
    end tell
end run
