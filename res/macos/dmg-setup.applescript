-- Lay out the disk image window: icons either side of the arrow drawn into the
-- background, and nothing else in the way.
--
-- CPack runs this as `osascript <this file> <volume name>` while the image is
-- still mounted read-write, then packages the .DS_Store that Finder writes out.
-- The coordinates match res/macos/dmg-background.tiff and have to be changed
-- together with it; scripts/make-dmg-background.py draws that file.
--
-- Build the release image in CI, not on a desktop running a tiling window
-- manager. Finder records the window it actually has, so yabai and its like
-- resize this window out from under the script and their geometry is what ends
-- up in the .DS_Store. Everything else here survives that -- the picture, the
-- icon size, the two positions -- but the window opens the wrong size.

on run argv
	set volumeName to item 1 of argv

	tell application "Finder"
		tell disk volumeName
			open

			set current view of container window to icon view
			set toolbar visible of container window to false
			set statusbar visible of container window to false
			-- {left, top, right, bottom}: a 660 x 400 content area, which is
			-- the size of the background picture.
			set the bounds of container window to {200, 120, 860, 520}

			set opts to the icon view options of container window
			set arrangement of opts to not arranged
			set icon size of opts to 128
			set text size of opts to 13
			set background picture of opts to file ".background:background.tiff"

			set position of item "CedarLogic.app" of container window to {165, 185}
			set position of item "Applications" of container window to {495, 185}

			-- Finder writes the .DS_Store when it is good and ready. Closing and
			-- reopening pushes it to do so, and the delay gives it time before
			-- CPack unmounts the image out from under it.
			close
			open
			update without registering applications
			delay 2
		end tell
	end tell
end run
