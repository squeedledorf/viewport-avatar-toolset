# VATs community content

Starter poses, clips and face mappings for Viewport Avatar Toolset (VATs), free for
anyone to use for anything. Everything in this repository is dedicated to the public domain under
[CC0 1.0](https://creativecommons.org/publicdomain/zero/1.0/): no credit needed, no conditions.

## Use it in VATs

1. Clone or download this repository:

   ```
   git clone <this repository's URL> vats-community
   ```

2. In VATs, open the **Inventory** tab and press **Add Community Folder...** under **Projects**. Choose the
   folder you cloned.
3. The poses, clips and animations are listed under **Animations**, one group per folder; the example projects
   under **Projects**. Right-click an animation and choose **Insert into Current Project at This Frame**, or
   drag it onto the view.

Pull the repository now and then (`git pull`) for new content. VATs reads the files again when the
Inventory gets the focus.

## Layout

| Folder | Contents |
|---|---|
| `poses/` | One-frame `.anim` files: a body or hand pose. |
| `clips/` | Short `.anim` files: a gesture, a nod, a step. |
| `animations/` | Whole `.anim` animations: loops, stands, dances. |
| `projects/` | `.vat` projects: examples with IK, pins and props, to open and study. |
| `face-mapping/` | One `.json` per mesh head: how face-capture shapes map onto that head's face bones. VATs does not read these yet. |

Name files in plain words, with the side where it matters: `wave-right-hand.anim`, `sit-cross-legged.anim`.

## Contributing

Open a pull request with your files. By contributing you dedicate them to the public domain under CC0 1.0.

**The rule: CC0 only, and never derived from bought content.**

- Only content you made yourself, from scratch, and are willing to give away under CC0.
- Nothing made from, traced from, retargeted from or edited from animations, poses, mocap or mesh heads you
  bought, or that were given to you under any licence other than CC0. That includes Marketplace animations,
  store AO parts, paid mocap packs and a head maker's own mapping files.
- Nothing ripped from a viewer's cache or from another avatar.
- Motion capture you recorded yourself is fine.

Pull requests that break the rule are closed. If you find something in this repository that breaks it,
open an issue and it is removed.

## Licence

CC0 1.0 Universal. Add the full legal text from
<https://creativecommons.org/publicdomain/zero/1.0/legalcode.txt> as `LICENSE` when publishing this
repository.
