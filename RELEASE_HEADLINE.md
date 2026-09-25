## Why

- The renderer was spending frames on detail that motion hides, and only on one axis. Halving both axes while moving costs a fraction of what it buys.
- The moving-half, standing-full snap had quietly stopped happening in July. It is back.

## Speed

- While you are moving, the renderer now halves both axes instead of only columns: one computed sample covers a 2x2 block. Roughly 20 to 25 percent faster in motion, and on lighter scenes that is enough to cross from 12 fps into 15.
- Stand still and it snaps back to full resolution, the same as before. The softening only ever appears while the picture is already moving.
- The two CPUs rebalance faster when the view changes. They used to correct their share of the screen in small steps, which kept up while you stood still and fell behind while you walked, leaving one processor idle at the frame's sync point in exactly the frames that juddered. Delivery is steadier as a result: the flap between rates dropped by about a third and the worst frames got noticeably shorter.
- The pass that draws lights, cutouts and crawlspace caps is now balanced across both CPUs on its own terms, instead of inheriting the split that was calculated for walls.
- The ceiling grid and crawlspace slab no longer draw rows that the display never shows.

## Frame pacing

- In a level, frames are held to a steady delivery rate instead of alternating between two, which is what made motion read as judder even when the average was fine. The rate adapts: sustained slow scenes step it down, and a sustained fast stretch steps it back up.
- This is why the speed work above shows up as a change in feel rather than a number climbing: the engine delivers on whole screen refreshes, so the win is crossing into a faster bracket rather than a gradual creep.

## Adaptive resolution

- AUTO is back to SCALE by default. Moving on a heavy frame drops the whole frame to half resolution, and holding still lifts it to full a couple of frames later.
- The LOD style is still there on the ADAPTIVE row in VISUALS. It renders full resolution at all times and re-blocks by depth, and it has no frame-time response, which is why the moving-to-standing snap disappeared when it became the default.

## Under the hood

- The frame is now fully accounted for. Every loop that flips a frame reports its own timing, including the attract and lobby loops, which previously reported nothing and made a large block of each frame look unexplained.
- Fixed a case where the second CPU could notice a render command up to 15 ms late because it was decoding audio at the time. Left switched off in this build pending a listening test.
