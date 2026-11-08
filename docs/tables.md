# tables/ — tables\ inventory, inferred format, index mapping (Phase 0)

Date: 2026-11-08. Status: Phase 0 recon — read-only except this file. `tables\` untouched.
Repo is source of truth; where prior summary differs from repo, repo wins (noted below).

> Removal note (post-Phase-0): the former single-phase IDA* solver has been
> removed from the tree (see git history). The `twophase` solver is now the
> only consumer of `tables\`. Historical sections below describe the
> pre-removal design; where they say "current", read "pre-removal".

## 0. Terminology: "tables" in this file

`tables\` (this folder, 34 files, no manifest) ≠ `data\` (our generated Korf
PDBs, `corner.db`/`edge*.db`). Do not conflate them. Names here are exact
filenames in `tables\`.

## 1. Inventory + SHA-256 per file + size vs anchors + first-64-byte hex

Evidence rules applied: filenames, `stat` sizes (`Get-ChildItem` Length),
README/manifest in folder (none — folder contains only the 34 files),
first 64 bytes only (hex below, via `File.OpenRead` + 64-byte read).
Full-file `cat` never performed. Hashes via `Get-FileHash -Algorithm SHA256`
(powershell `Get-ChildItem | Get-FileHash`).

### 1.1 `tables\` (34 files)

| # | File | Size (bytes, stat) | SHA-256 |
|---|---|---|---|
|1|pieceCubeMovementHTM|1,008|`1ef2c994a1d42e64e4c423f05618c4f2baa0cb473347b5e5c393f5c1cb0e7b48`|
|2|pieceCubeMovementQTM|1,008|`1ef2c994a1d42e64e4c423f05618c4f2baa0cb473347b5e5c393f5c1cb0e7b48`|
|3|pieceCubeMovementSTM|1,512|`143d7e6c9cf3cbb6e8a37a167dceec78a75d1892be5dae38811a03501483960a`|
|4|pieceCubeMovementATM|2,520|`9e5209a01c7b45e74b7470460418313c8bd3ab3998b8e2f383856b03c5a846d0`|
|5|kociembaPhase1MovementHTM|2,015,640|`251a48f93106c63787756b3e8b0740ae421cba7f06d1b7ccd6cc9b7805a4138a`|
|6|kociembaPhase1MovementQTM|2,015,640|`251a48f93106c63787756b3e8b0740ae421cba7f06d1b7ccd6cc9b7805a4138a`|
|7|kociembaPhase1MovementSTM|3,023,460|`47074151dbb606659993b5ac9675ccfba3b31d38675258c017979a695448604a`|
|8|kociembaPhase1MovementATM|5,039,100|`34f6290ec1e2b263c77e627124be0ccfba55ff4f84b9d3cff46c79f1352c6cf4`|
|9|kociembaPhase2MovementHTM|3,226,560|`27cead0b3955d4d3dee7ae7679ea48eb0d03e7b551ff056e593dd88e47ad3abb`|
|10|kociembaPhase2MovementQTM|3,226,560|`27cead0b3955d4d3dee7ae7679ea48eb0d03e7b551ff056e593dd88e47ad3abb`|
|11|kociembaPhase2MovementSTM|4,839,840|`0b31b4bea87f547bc29e7bd617b2c650e1dadc375ccf44de693ed2d0a8918b00`|
|12|kociembaPhase2MovementATM|6,775,776|`c2d4574b43bba33befe697ec162b5fb62af64e448855f017223ee800c17b3264`|
|13|cornerHeuristicHTM|44,089,920|`72a5584777650038220389627c8f649d86b37b7b1b80d3ec4a070878b68a28cd`|
|14|cornerHeuristicQTM|44,089,920|`ef1ecb54ed0759aca8f4024b96fdd75d629d3f1b93a7cc96b53af080466086ab`|
|15|cornerHeuristicSTM|44,089,920|`1bee584c9acf0e2211e4c1fdaa4c2e4d68e440d75dbb8046e9c792e4cbc33f54`|
|16|cornerHeuristicATM|44,089,920|`ba9f3fdc129819b05373f4828e9c09873b574ad6c891a32c626a9aad75571faf`|
|17|edgePermutationHeuristicHTM|239,500,800|`5a1cec6ec3cb169886b79f39d7fee157083e71e6a240192b8a6ea49cbbbeaaca`|
|18|edgePermutationHeuristicQTM|239,500,800|`2e43880bede7b8ec5777e5b539e1382f9fffe1fa4760b246ef7f13b2d8021f42`|
|19|edgePermutationHeuristicSTM|239,500,800|`7fb8832463460490f7bacd433273047e933b41cb6f9210cc6ff260b077c6a2ee`|
|20|edgePermutationHeuristicATM|239,500,800|`e2d4e6a56053ec6dadabb736f46b2c58b465f70b67b628471b0d89568d70ce43`|
|21|kociembaPhase1HeuristicHTM|100,776,960|`cff8a49ae535c9fe1f3d590c7a34f4726990cf628a125263724629117360a9cf`|
|22|kociembaPhase1HeuristicQTM|100,776,960|`a278aa34e65e91b2e8311d927dd430cf2ee6372d67f94327dcdfb5a2191700ae`|
|23|kociembaPhase1HeuristicSTM|100,776,960|`d32d498fc7e44e8176361320639c188bdaad4519b20353af586d3dd39dfb83f5`|
|24|kociembaPhase1HeuristicATM|100,776,960|`40d065bfadea442bf7b7337190834e28c5895404fc9c857836fccc33e2da9f3f`|
|25|largeKociembaPhase1HeuristicHTM|2,418,647,040|`f5c36e47f04954310a9830d9a93afda97545e4d9e9f7f96fb37e1393459b590b`|
|26|largeKociembaPhase1HeuristicQTM|2,418,647,040|`fb51a5c8b1c2df1986db332e31c02b09e6066d9af59ce01bf047b3172da9a652`|
|27|largeKociembaPhase1HeuristicSTM|2,418,647,040|`6515beda05c63d1b6b412e7831416097e7ada80cd0f6d48f28eb5a4518c11cba`|
|28|largeKociembaPhase1HeuristicATM|2,418,647,040|`70b1c25f07039140a2e4d1d9e64e768da3801e43d8dc57363d94c7f25b68d3ca`|
|29|kociembaPhase2HeuristicHTM|334,817,280|`cc713b1d5b241ba5d66fa76b56d0947afc4da283e3979e9161f5c5b3557fcb5b`|
|30|kociembaPhase2HeuristicQTM|334,817,280|`e2e7b37a00c8c37e3d0f9e4ead689a0b2162c8708940c99b43945e45d760dfce`|
|31|kociembaPhase2HeuristicSTM|334,817,280|`9da08a785d9c689cd9e6f07a717afadcd3e77a35781da7cc81297953ab6514b8`|
|32|kociembaPhase2HeuristicATM|334,817,280|`20eb576f27b2d578f93bd5d4e92a51c284b16bf74124ca38384c9d877f47fcd9`|
|33|kociembaPhase1Symmetry|65,783,403|`a254654c03fab69a8eccc56f56434d5f805a7744d6b6d69a8298ce09f484a6eb`|
|34|kociembaPhase2Symmetry|5,364,096|`371d753fb475848e4569c20d0ccfb6655a26de8d98cff965ce837d62579b617b`|

Notes from stat:
- Total ≈ 4.3 GB on disk (dominated by 4× `large*` at 2.42 GB each).
  All 34 hashes match the upstream public table page verbatim (see §7),
  including the 3 identical HTM/QTM movement pairs.
- No README/manifest inside `tables\` (34 data files only).

First-64-bytes hex (lowercase, space-separated, as read; up to 64 bytes/file):

- cornerHeuristicATM: `a0 aa a9 aa a9 a9 8a aa 88 aa a9 88 aa a8 a9 88 a7 89 88 87 9a 98 aa 89 98 8a 96 aa a9 98 78 a8 98 98 86 9a 78 88 9a 98 aa a9 8a aa 88 8a aa b9 98 a8 88 98 88 98 99 99 99 aa 97 99 99 99 89 aa`
- cornerHeuristicHTM: same 64 bytes as ATM (hashes differ later in file)
- cornerHeuristicQTM: `c0 cc cc cc cc cc ac ca aa cc cc aa ac ca cc aa ca 8a aa aa cc c8 ca 8a c8 8a ca cc cc a8 aa ca a8 c8 8a ca aa 8a ca ca cc cc ac ca aa ac ca cc cc c8 aa cc c8 c8 cc cc cc ca ca cc ac cc 8c ca`
- cornerHeuristicSTM: same 64 bytes as HTM/ATM
- edgePermutationHeuristicATM: `70 67 76 77 16 66 66 67 61 61 76 76 67 46 54 54 65 66 55 54 46 56 43 46 54 65 55 67 34 45 45 66 64 64 56 64 65 34 66 54 57 63 66 45 44 44 64 65 66 45 44 43 75 56 56 54 46 66 44 36 57 65 46 45`
- edgePermutationHeuristicHTM: `70 67 86 87 16 66 66 68 61 61 76 86 67 46 54 54 65 66 55 64 46 56 43 46 54 65 55 67 34 46 45 66 64 64 56 64 65 34 66 64 57 63 66 45 44 44 64 67 66 46 44 43 75 76 56 54 46 66 44 36 78 67 66 65`
- edgePermutationHeuristicQTM: `70 67 96 87 16 67 76 69 72 61 76 87 67 56 65 74 65 76 65 76 47 58 43 56 74 85 56 67 34 67 56 67 74 65 56 65 65 36 67 74 67 74 67 56 45 54 65 78 76 47 54 63 76 87 56 65 56 67 54 47 89 78 87 76`
- edgePermutationHeuristicSTM: `70 67 86 77 16 66 66 68 61 61 76 76 67 46 54 54 65 66 55 64 46 56 43 46 54 65 55 67 34 46 45 66 64 64 56 64 65 34 66 64 57 63 66 45 44 44 64 67 66 46 44 43 75 76 56 54 46 66 44 36 58 65 46 45`
- kociembaPhase1HeuristicATM: `76 67 76 77 67 77 66 77 77 86 77 67 76 77 77 77 87 77 77 66 77 76 77 77 77 76 67 77 77 77 77 76 77 67 76 76 77 66 77 77 76 76 76 67 77 76 67 66 77 76 77 77 67 77 76 77 77 77 67 77 67 76 77 77`
- kociembaPhase1HeuristicHTM: `99 89 99 a9 89 a9 89 99 99 a8 99 98 99 99 99 99 a9 99 99 88 98 99 aa 99 99 99 9a 99 a9 89 a9 98 99 99 99 89 99 98 9a 99 99 99 98 88 89 98 98 88 99 99 98 88 89 99 99 98 99 99 99 98 99 89 99 99`
- kociembaPhase1HeuristicQTM: `bb ab cb cb ab bb aa bb bb ca bb ba bb bb bb bb cb bb bb ab bb bb cc cb bb bc bc ba ba aa ba ca bb bb bb bc bb ba bc bb ab aa ba aa aa ba aa aa aa bb ba aa 9b ab ba ba aa ab bb b9 9a ab ab aa`
- kociembaPhase1HeuristicSTM: `86 89 98 99 89 99 88 87 88 98 88 88 78 98 88 88 98 88 88 88 88 88 98 88 88 78 88 88 98 89 98 98 89 87 88 88 88 88 88 78 89 87 87 88 88 87 88 77 88 98 87 88 78 87 87 98 88 88 88 88 88 88 88 88`
- kociembaPhase1MovementATM: 64× `00` except bytes 58–59 `0e 06` (i.e. 53 zero bytes then `0e 06 00 00` as first non-zero u32 LE)
- kociembaPhase1MovementHTM: `00`×22 then `0e 06 00 00 00 00 00 00 0e 06 00 00 1f 02 00 00 00 00 00 00 1f 02 00 00 9c 01 00 00 00 00 00 00 9c 01 00 00 06 03 00 00` (u32 LE: 0,0,0,0,0,1550,0,1550,543,0,543,412,0,412,774…)
- kociembaPhase1MovementQTM: identical 64 bytes to HTM
- kociembaPhase1MovementSTM: identical 64 bytes to HTM
- kociembaPhase1Symmetry: 64× `00`
- kociembaPhase2HeuristicATM: `54 55 55 aa aa aa aa aa aa 55 55 55 55 55 55 00 00 00 aa aa aa 02 00 00 55 55 55 aa aa aa 55 55 55 55 55 55 55 55 55 55 55 55 00 00 00 55 55 55 01 00 01 55 55 55 aa aa aa 55 55 55 55 55 55 aa`
- kociembaPhase2HeuristicHTM: `00 00 02 aa 80 20 aa 80 20 42 00 00 42 00 00 24 aa 9a aa 80 20 40 00 41 02 00 40 aa aa a2 00 01 04 42 00 00 02 00 40 00 01 04 61 aa 8a 02 00 40 15 55 16 00 01 04 aa a8 aa 42 00 00 02 00 40 aa`
- kociembaPhase2HeuristicQTM: `28 aa 28 aa aa aa aa aa aa 80 00 80 80 00 80 00 00 00 aa aa aa 6a aa 69 80 00 80 00 00 00 aa a9 a6 80 00 80 80 00 80 aa a9 a6 00 00 00 80 00 80 a8 aa aa aa a9 a6 00 00 00 80 00 80 80 00 80 aa`
- kociembaPhase2HeuristicSTM: `54 55 55 aa 80 20 aa 80 20 a9 95 a6 a9 95 a6 24 aa 9a aa 80 20 42 51 50 a9 66 96 9a 69 a6 55 56 59 95 6a 9a a9 66 96 55 56 59 61 aa 8a 95 99 aa 01 00 02 55 56 59 9a 69 a6 95 6a 9a 95 99 aa aa`
- kociembaPhase2MovementATM: `d8 27 00 00 28 41 00 00 50 19 00 00 0a 00 00 00 e2 27 00 00 32 41 00 00 5a 19 00 00 17 00 00 00 ef 27 00 00 3f 41 00 00 67 19 00 00 0d 00 00 00 e5 27 00 00 35 41 00 00 5d 19 00 00 af 11 00 00`
- kociembaPhase2MovementHTM: `d8 27 00 00 28 41 00 00 50 19 00 00 0a 00 00 00 17 00 00 00 0d 00 00 00 af 11 00 00 90 7a 00 00 c9 02 00 00 d6 6e 00 00 d9 27 00 00 29 41 00 00 51 19 00 00 0b 00 00 00 16 00 00 00 0c 00 00 00`
- kociembaPhase2MovementQTM: identical 64 bytes to HTM
- kociembaPhase2MovementSTM: `d8 27 00 00 28 41 00 00 50 19 00 00 0a 00 00 00 17 00 00 00 0d 00 00 00 af 11 00 00 90 7a 00 00 c9 02 00 00 d6 6e 00 00 e5 27 00 00 3f 41 00 00 5a 19 00 00 50 89 00 00 6f 70 00 00 d9 27 00 00`
- kociembaPhase2Symmetry: 64× `00`
- largeKociembaPhase1HeuristicATM: `76 66 67 67 67 66 66 76 76 76 66 67 77 77 77 77 77 77 77 77 77 77 77 77 77 77 77 77 77 77 77 77 77 77 77 76 76 77 77 77 77 77 77 77 77 67 67 67 77 76 77 77 77 77 67 67 77 77 77 87 87 88 88`
- largeKociembaPhase1HeuristicHTM: `aa 9a 9a 9a 9a 99 9a aa a9 aa aa aa a9 aa aa aa aa aa aa 99 9a 9a 99 a9 99 9a 99 99 aa aa 9a 9a aa 9a 9a a9 98 99 9a a9 9a 9a aa 99 9a 9a 89 9a 99 a9 a9 9a aa a9 9a 9a 99 aa 9a 9a a9 aa aa aa`
- largeKociembaPhase1HeuristicQTM: `cc cc dc dc cb cc cc bc bc cc cc cb cc cc cd cb cc cc cc bc bc bb cb cb bc cc cb cb cb cc cc bb bc bc cc cc bb bc cc ca bb bc bb bb bc ac bc cc bb cb cb cc cb cc cc bc bc cc bc bb dd cc cd cc`
- largeKociembaPhase1HeuristicSTM: `77 67 67 67 67 66 67 77 76 77 77 77 99 89 9a 89 89 88 89 99 99 99 99 99 99 99 99 99 99 99 99 99 99 99 99 98 99 99 99 99 99 99 99 99 99 89 99 89 89 88 89 99 98 89 88 88 99 88 89 99 99 99 99`
- pieceCubeMovementATM: `08 0c 04 00 08 0c 04 00 08 0c 04 00 08 0c 04 00 00 00 12 12 12 12 18 18 18 18 0a 0a 0a 0a 00 00 00 05 05 05 05 14 14 14 14 11 11 11 11 09 0d 05 01 09 0d 05 01 09 0d 05 01 09 0d 05 01 01 01 10`
- pieceCubeMovementHTM: `08 0c 04 00 00 00 00 00 00 12 18 0a 00 00 00 05 14 11 09 0d 05 01 01 01 01 01 01 10 19 08 01 01 01 06 15 12 0a 0e 06 02 02 02 02 02 02 11 1a 09 02 02 02 04 16 10 ff ff ff ff ff ff ff ff ff ff`
- pieceCubeMovementQTM: identical to HTM
- pieceCubeMovementSTM: `08 0c 04 00 00 00 00 00 00 12 18 0a 00 00 00 05 14 11 08 0c 04 12 18 0a 11 14 05 09 0d 05 01 01 01 01 01 01 10 19 08 01 01 01 06 15 12 09 0d 05 10 19 08 12 15 06 0a 0e 06 02 02 02 02 02 02 11`

### 1.2 `data\` (our PDBs, for anchors)

| File | Size (bytes, stat) | SHA-256 (first 64B head) |
|---|---|---|
|corner.db|44,089,960|`A22AC9FBCAFDB808290387BC2539D65C36521E11C5CB21C4F70EC69DDC970CFE`|
|edge1.db|21,289,000|`9AC5522F0DCB3DBA1756DB8C900967B77FCDF029395DFF64683EBE5299C7C818`|
|edge2.db|21,289,000|`25D61CB76395CC584381684F85546FFDFF2BBAE2BCE7C0A2A57259E13A6CD052`|
|edge3.db|21,289,000|`6BCCD1DB6657562A7BAF29987ECF5791C5DA91F3A95133064284F679DB66302F`|

`data\corner.db` first 64 bytes (header+payload start):
`52 50 44 42 01 00 00 00 00 00 00 00 00 00 00 00 80 84 41 05 00 00 00 00 40 c2 a0 02 00 00 00 00 0b 00 00 00 00 00 00 00 a0 aa a9 aa a9 aa 89 a8 8a aa aa a8 89 a8 a8 89 a9 87 88 88 9a 97 aa 88`
Decodes as 40-byte `RPDB` header (magic, version=1, pdb_id=0, entries=0x0005418480=88,179,840, data_bytes=0x02a0c240=44,089,920, max_dist=0x0b) then payload. Same for edge files with pdb_id 1/2/3, entries 0x0289b000=42,577,920, data_bytes 0x0144d800=21,288,960, max_dist 0x0a.

### 1.3 Size vs anchors

Anchors from task background:
- 88,179,840 entries at 4 bits = 44,089,920 bytes, at 8 bits = 88,179,840 bytes.
- 42,577,920 entries at 4 bits = 21,288,960 bytes. Allow header.

| Table family | File size | Anchor comparison |
|---|---|---|
|cornerHeuristic* (all 4) | 44,089,920 | = 88,179,840 × 4 bits exactly. No header. Same payload size as `data\corner.db` (44,089,960 = payload + 40-byte `RPDB` header), but contents differ (see §4) — same size, different ranking. |
|edgePermutationHeuristic* | 239,500,800 | = 479,001,600 (=12!) × 4 bits (479,001,600/2). NOT our edge anchor (42,577,920 × 4 bits = 21,288,960). This is full edge permutation, not 6-edge PDB. |
|kociembaPhase1Heuristic* | 100,776,960 | = 201,553,920 × 4 bits. 201,553,920 = 45 × 2187 × 2048. Not full 495 × 2187 × 2048 = 2,217,093,120 (that would be 1,108,546,560 bytes). 45 = #equivalence classes of the 495 middle-layer positions under cube symmetry (495/45 = 11× reduction). See §2. |
|largeKociembaPhase1Heuristic* | 2,418,647,040 | = 4,837,294,080 × 4 bits = 24 × 201,553,920 = 45 × 24 × 2187 × 2048. 24 = slice-permutation 4!. |
|kociembaPhase2Heuristic* | 334,817,280 | = 1,339,269,120 × 2 bits (= entries/4). 1,339,269,120 = 2768 × 40320 × 12 (= 2768 × 40320 × 24/2). 2768 = #equivalence classes of 40320 corner perms. Note: nibble hypothesis would give 669,634,560 entries — wrong; upstream uses 2 bits/entry (values mod 3, `0b11` = unreached). First-64 hex values `00 00 02 aa 80 20…` confirm ≤2-bit packing. |
|pieceCubeMovement* | 1,008 / 1,512 / 2,520 | = 56 × moveCount (32+24 bytes/move). moveCount: HTM 18, QTM 18, STM 27, ATM 45. HTM/QTM identical files (see §2). |
|kociembaPhase1Movement* | 2,015,640 / 3,023,460 / 5,039,100 | = 111,980 × moveCount. 111,980 = 2187×4 + 2048×4 + 11880×8 = 8,748+8,192+95,040. |
|kociembaPhase2Movement* | 3,226,560 / 4,839,840 / 6,775,776 | = 322,656 × moveCountPhase2. 322,656 = (40320+40320+24)×4. moveCountPhase2: HTM 10, STM 15, ATM 21. |
|kociembaPhase1Symmetry | 65,783,403 | = 139,968 + 760,320 + 64,880,640 + 1,980 + 495 = 65,783,403. Odd length proves concatenated components, not pure nibble array. |
|kociembaPhase2Symmetry | 5,364,096 | = 2,580,480 + 2,580,480 + 1,536 + 161,280 + 40,320 = 5,364,096. |

Header presence: `tables\` files have NO header. `saveSingleLookupTable`
writes components sequentially with `fwrite` (no magic/version). Sizes match
raw concatenated components exactly, with no 40-byte `RPDB` header. The
40-byte header exists only in our `data\` format (see §5 loader plan).

## 2. Inferred format (header?, entry width, endianness)

Upstream provenance was located from filenames (see §7): all 34 SHA-256
values match the public table page verbatim. Read-only fetch of the
upstream C source (`src/lookupTables/lookupTables.h/.c`,
`generate*.c`, `encode/*.c`, `common.h`) confirms the layout below.
Local first-64 evidence is consistent with it (movement files start with
`u32`/`u8` values, heuristics show nibble/2-bit packing, symmetry files
start with zeroed `u32` blocks).

General:
- No file header, no magic, no version. Files are raw `fwrite` dumps of
  `malloc`'d arrays in C order. Components are concatenated in the order
  listed in `lookupTables.c` (`pieceCubeMovementTableComponents`, etc.).
- Endianness: native little-endian (x86-64 / Ubuntu 24.04, GCC). All
  `u32`/`u64` components read as little-endian integers on Windows
  x64 as well. No big-endian conversion.
- Move metrics (suffix): `HTM`=Half-Turn (18 moves, phase2 10),
  `QTM`=Quarter-Turn (18 moves, phase2 10), `STM`=Slice-Turn (27 moves,
  phase2 15), `ATM`=Axial-Turn (45 moves, phase2 21). Symmetry files have
  no suffix (metric-independent). HTM/QTM movement files are byte-identical
  because movement tables store geometry (cubie transitions); the metric
  changes only the heuristic distances (which do differ — cornerHeuristic
  QTM first-64 differs from HTM).

Table-by-table (file → components concatenated in order, no header):

- `pieceCubeMovement{HTM,QTM,STM,ATM}` = `u8[32×moveCount]` (pieceCubeCorners)
  + `u8[24×moveCount]` (pieceCubeEdges). Entry: packed cubie value
  (`(cubie<<2)|twist` for corners, `(cubie<<1)|flip` for edges), 8-bit.
  Layout is `[index×moveCount + move]` (see `generateMovementTables.c`).
  First-64 shows `08 0c 04 00…` (u8 cubie encodings) and `ff` init padding
  visible in HTM at offset ~55 (remainder of last initialized region; files
  are `memset(0xff)` then filled).

- `kociembaPhase1Movement*` = `u32[2187×moveCount]` cornerOrientations +
  `u32[2048×moveCount]` edgeOrientations +
  `u64[11880×moveCount]` middleEdges (11880 = 495×24 = slice positions ×
  slice permutation, i.e. `P(12,4)`). Layout `[index×moveCount+move]`.
  First u32 LE in HTM: 0,0,0,0,0,1550,0,1550,543,0,543,412,0,412,774…
  (zeros = moves that map to solved orientation for early indices;
  non-zero = coordinate successors). `u64` tail explains why ATM is
  exactly 2.5× HTM (45/18) and STM 1.5× (27/18).

- `kociembaPhase2Movement*` = `u32[40320×moveCountPhase2]` cornerPermutations +
  `u32[40320×moveCountPhase2]` UDEdgePermutations +
  `u32[24×moveCountPhase2]` EEdgePermutations. Layout `[index×moveCountPhase2+move]`.
  First u32 LE (HTM): 10200 (0x27d8), 16680 (0x4128), 6480 (0x1950), 10,
  10210, 16690, 6490, 23… — all <40320, successors in permutation space,
  advancing by ~10 per row-consistent with 10 phase-2 moves.

- `cornerHeuristic*` = `u8[88,179,840/2]` = 44,089,920 entries nibble-packed,
  4 bits/entry, `entry = (byte[idx>>1] >> ((idx&1)*4)) & 0xf`. Sentinel
  `0xf` = unreached during BFS. Max depth fits in 4 bits.

- `edgePermutationHeuristic*` = `u8[479,001,600/2]` nibble-packed 4 bits,
  entry index = 12! permutation of all 12 edges (edge permutations only,
  orientation ignored). Same sentinel `0xf`. Solved at index 0.

- `kociembaPhase1Heuristic*` (reduced) = `u8[201,553,920/2]` nibble-packed
  4 bits. Index = `((class45 × 2048) + flip2048') × 2187 + twist'` where
  `class45` (0..44) is the symmetry-equivalence class of the 495
  middle-layer positions, and `flip/twist` are symmetry-reduced via the
  Symmetry tables (see §3). Full table (495×2187×2048) would be
  1,108,546,560 bytes — not what is on disk. File is exactly 1/11 of that.
  Sentinel `0xf`.

- `largeKociembaPhase1Heuristic*` = `u8[4,837,294,080/2]` nibble-packed.
  Same as above but `×24` for slice-permutation: `(((class45×2048 + flip)×2187 + twist)×24) + slicePerm24`.
  Header comment in upstream: full would be 53,210,234,880 = 2187×2048×(12!/8!);
  after 45-class reduction = 4,837,294,080 entries = 2.4 GB on disk.

- `kociembaPhase2Heuristic*` (reduced) = `u8[1,339,269,120/4]` 2 bits/entry,
  4 entries/byte: `entry = (byte[idx>>2] >> ((idx&3)*2)) & 0b11`. Values
  stored mod 3 with `0b11` (3) = unreached. Index =
  `(class2768 × 40320 + UD-perm40320) × 12 + (slicePerm24>>1 parity)`,
  i.e. `Q >> 1` of `(class×40320 + edgePerm)×24 + slicePerm` — the `/2`
  is the parity constraint (corner parity ^ edge parity ^ slice parity
  must be even). Full table would be 19,508,428,800 entries; we store
  1,339,269,120 (reduced by 2768 classes, half by parity).

- `kociembaPhase1Symmetry` (no metric suffix, no header) =
  `u32[2187×16]` cornerOrientation symmetries (139,968 B) +
  `u32[11880×16]` middleEdges symmetries (760,320 B) +
  `u32[1,013,760×16]` edgeOrientation symmetries (64,880,640 B) +
  `u32[495]` equivalenceClassValues (1,980 B) +
  `u8[495]` minOrientation (495 B). Total 65,783,403 ✓.
  First 64 bytes all `00` (solved orientation's 16 symmetric successors are 0).

- `kociembaPhase2Symmetry` =
  `u32[40320×16]` cornerPermutation symmetries (2,580,480 B) +
  `u32[40320×16]` UDEdgePermutation symmetries (2,580,480 B) +
  `u32[24×16]` EEdgePermutation symmetries (1,536 B) +
  `u32[40320]` equivalenceClassValues (161,280 B) +
  `u8[40320]` minOrientation (40,320 B). Total 5,364,096 ✓.
  Also starts 64× `00`.

Entry widths summary: heuristics nibble (4-bit) except Phase-2 heuristic
2-bit; movement `u8`/`u32`/`u64` depending on component; symmetry `u32`+`u8`
mix. No header in any `tables\` file.

## 3. Index-function hypotheses (cube state → entry)

### 3.1 Our frozen core (`src/core/cube.hpp`; pre-removal `solver.hpp/.cpp`, see git history)

Existing (frozen) index functions of the pre-removal solver:

- Cubies: `CP[0..7]`/`CO[0..7]`, `EP[0..11]`/`EO[0..11]` in Kociemba order
  (corners URF,UFL,ULB,UBR,DFR,DLF,DBL,DRB; edges UR,UF,UL,UB,DR,DF,DL,DB,FR,FL,BL,BR).
  Centers `centers[0..5]` identity for face moves.
- Corner PDB: `corner_index = permRank(cp) × 2187 + orientIndex(co[0..6] base-3)`,
  `permRank` factorial number system (Lehmer), `orientIndex` base-3 LSB-first.
  Solved maps to 0.
- Edge PDBs 0..2: `edge_index = rank(positions of 6 distinguished cubies) × 64 + orientBits`,
  `kEdgeCubies = {0..5}, {6..11}, {0,2,4,6,8,10}`, rank via
  `P(12,6)=665280` factorial blocks `55440,5040,504,56,7,1`, `orientBits` bit d
  = `EO` at position of distinguished cubie d. Solved index generally non-zero;
  `solved_pattern_index(pdb_id)` is the BFS origin.
- Packing: byte `idx/2`, low nibble even, high nibble odd. Distances capped at
  14 with 15 sentinel; stored 15 never overestimates.

### 3.2 Upstream index functions (hypotheses to verify — source-read, not in repo)

Upstream uses different cubie numbering and different ranking, so indexes
are NOT directly interchangeable with §3.1. These are required to reuse
`tables\` at all; all are implementable in C++23 using `cube.hpp` as the
State→cubie conversion step (centers + facelet conversion already there).

**PieceCube encoding (bridge).** Upstream `encodeCornerState`/`encodeEdgeState`
map sticker cube to `corners[8]=(cubie<<2)|twist(0..2)`,
`edges[12]=(cubie<<1)|flip(0..1)` with their own order:
corners UBL,UFL,UBR,UFR,DBL,DFL,DBR,DFR (permutation arrays
`order=[2,0,6,4,3,1,7,5]`) and edges UB,UR,UF,UL,FL,FR,BR,BL,DB,DL,DF,DR.
A fixed permutation map `cube.hpp` CP/EP ↔ upstream PieceCube is needed;
it must be derived from sticker or cubie identity, not guessed.

**cornerHeuristic*:** `encodeAllCorners = permIndex(cp-Upstream) × 2187 + orient…`
But orientation encoding differs from §3.1: upstream packs orientations
of cubies 5,6,7,0,1,2,3 in that order (see `encodeAllCorners`:
`(((perm×3+co5)×3+co6)…+co3)`), i.e. `orientIndex = ((co5×3+co6)×3+… )`
with pieces 5,6,7 first — not `co[0..6]` LSB-first as in `corner_orient_index`.
Permutation index is also Lehmer-style (`eightValuePermutationIndex`)
on reordered cubies. Solved maps to 0. Hypothesis: identical rank domain
(40320×2187) but different digit order. Must be re-implemented exactly or
every lookup is off by a permutation of indexes.

**edgePermutationHeuristic*:** `encodeAllEdges` (12! permutation) — analog
of Lehmer on 12 edges, orientation stripped (`>>11` in generator, i.e.
`edgePerm = encodeAllEdges >> 11`). Solved = 0. Our repo has no 12!-rank;
new function needed (full factorial rank on EP).

**Phase-1 coordinates (for `kociembaPhase1Movement*` and both Phase-1 heuristics):**
- `twist = encodeCornerOrientation`: sum of `cornerValues[pieceCube.corners[i]]`
  with table `0,0,0,0, 0,729,1458,2187, 0,243,486,729, 0,81,162,243, 0,27,54,81,
  0,9,18,27, 0,3,6,9, 0,1,2,3` — i.e. base-3 with shuffled weights,
  not contiguous `co[0..6]`.
- `flip = encodeEdgeOrientation`: 11-bit field via `edgeTableValues`
  (bit positions scattered), MSB-last corner excluded.
- `middleLayer = encodeMiddleLayer`: `comb495 × 24 + perm24`, where `comb495`
  encodes positions of the 4 mid-slice edges (FL,FR,BR,BL in upstream order)
  via combination tables (`edgeCombinationValues`), and `perm24` encodes
  their relative order. `middleLayer/24 = comb495`, `middleLayer%24 = perm24`.
  Solved middleLayer=1656, solved combination=69.

**Phase-1 heuristic index (reduced):**
`sym = minOrientation[comb495]` (from Phase1Symmetry `u8[495]`).
`midSym = middleEdgesSym[middleLayer×16 + sym]` (u32[11880×16]).
`class45 = equivalenceClassValues[midSym/24]` (maps 495→45).
`flipSym = edgeOrientationSym[(flip×495 + comb495)×16 + sym]`
`twistSym = cornerOrientationSym[twist×16 + sym]`
`index = ((class45×2048) + flipSym)×2187 + twistSym`
Large index = `(index×24) + (midSym%24)`.
Note: verification must reimplement all three Symmetry tables lookups;
the "sym" choice is data-driven, not a fixed 16.

**Phase-2 coordinates:**
- `cornerPerm = encodeCornerPermutation` (8-value perm index on upstream corners).
- `UDperm = encodeUDEdgesPermutation` (perm of 8 UD edges, returns -1 if invalid).
- `slicePerm = encodeMiddleEdgePermutation` (4-value perm index).
- Index: `sym = minOrientation2[cornerPerm]` (Phase2Symmetry u8[40320]);
  `getPhase2SymmetricPosition` rewrites cube under that orientation;
  `class2768 = equivValues2[symCornerPerm]`;
  `raw = ((class2768×40320) + symUDperm)×24 + symSlicePerm`;
  `index = raw >> 1` (parity halving).
- 2-bit packing: `byte[idx>>2]`, `(idx&3)*2` shift, value mod 3, `0b11` sentinel.

**Movement tables:** successors are `[coordIndex×moveCount + move]`
(Phase-1) / `[coordIndex×moveCountPhase2 + move]` (Phase-2). PieceCube
movement: `pieceCubeCorners[corners[0-node]×moveCount + move]` etc. —
single-cubie transitions from which full-cube `applyTurnToPieceCube`
composes. Move order is upstream `moveMetrics[metric].moves[]`, not our
`all_face_moves()` 18-order, and includes slice/axial moves (STM 27, ATM 45).
QTM movement files being identical to HTM is expected (same 18 face moves
in storage; QTM weighting lives in heuristics only).

Open assumption to freeze in P1: the exact permutation map between
`cube.hpp` cubie order and upstream order, including upstream's
`unorderCornerPermutations=[1,5,0,4,3,7,2,6]` reordering. All index
functions above depend on it.

## 4. Proposed mapping tables→solvers

This was written before the two-phase solver existed (§8 `docs/SPEC.md`
pre-removal described a single-phase IDA* + 4 Korf PDB solver). As of the
removal, `src/solvers/twophase/` consumes these tables and the single-phase
solver is gone.

- `cornerHeuristic{HTM,QTM,STM,ATM}` → **single-phase solver (additional admissible heuristic; pre-removal proposal)**.
  Same domain as our corner PDB (88,179,840 corner patterns) but different
  index order. Could either replace `data\corner.db` (after re-index) or be
  used as a 5th heuristic `h_corner = max(h_corner, h_tables)` if the
  two rankings agree on distance values. Requires: reimplement
  `encodeAllCorners` on `cube.hpp` state and prove admissibility
  (`h_corner(state) ≤ BFS-true(distance)`). HTM is the only metric
  compatible with our frozen 18-face-move HTM solver; QTM/STM/ATM variants
  are inadmissible for HTM distance and must NOT be `max()`'d into an HTM
  heuristic (they would over-estimate). ATM/QTM/STM files are useful only
  if the solver itself is generalized to those metrics, which is out of
  scope for the frozen interface.

- `edgePermutationHeuristic*` → **single-phase solver (additional heuristic; pre-removal proposal)**.
  Full 12! edge-permutation distance. Stronger than any single 6-edge PDB
  in isolation but 240 MB. Usable as `h_edgePerm` in `max()` with HTM only.
  Requires new 12!-rank + `encodeAllEdges` equivalent.

- `kociembaPhase1Heuristic{HTM}` → **single-phase solver (additional pruning; pre-removal proposal)**.
  Distance to G1 (orientation+slice). Admissible for full-cube distance
  (lower bound). Upstream uses it ×6 (3 orientations × {forward,inverse},
  max of 6) — that orientation-max trick needs whole-cube rotations,
  which our `cube.hpp` supports via `centers`. Could be a 5th/6th heuristic.
  Requires Symmetry tables + Phase-1 coordinate encoder.

- `largeKociembaPhase1Heuristic{HTM}` → **single-phase solver (stronger Phase-1 heuristic; pre-removal proposal; the tables are now used by `twophase` instead)**.
  Same as above but slices permuted (24×). 2.4 GB file — loading into RAM
  on this machine needs verification (upstream requires 3 GB RAM). Recommend
  deferring until Phase-1 heuristic validates; not needed for MVP.

- `kociembaPhase2Heuristic{HTM}` → **not usable by the single-phase IDA***
  (solves G1→solved, meaningful only inside G1). It is a Phase-2 PDB
  (mod-3 packing, parity-halved indexes). It is now used directly by the
  `twophase` solver via upstream's Phase-2 IDA* (successor = Phase-2
  movement tables, heuristic = Phase-2 heuristic mod-3 trick), with the
  parity/equivalence-class logic implemented in `src/solvers/twophase/`.

- `kociembaPhase1Movement*` / `kociembaPhase2Movement*` /
  `pieceCubeMovement*` → **supporting tables for whichever solver uses the
  heuristics** (O(1) coordinate successors). Without them, index functions
  alone cannot step the search efficiently; our `corner_successor`/`edge_successor`
  equivalents would need `apply()` + re-rank per node (slow). HTM variants
  only (other metrics incompatible with frozen 18-move set).

- `kociembaPhase1Symmetry` / `kociembaPhase2Symmetry` → **required
  companions, not standalone heuristics**. They encode the 16 cube
  symmetries (48 with reflections upstream reorients to 16 classes) and
  the 495→45 / 40320→2768 class reductions. Loading them is mandatory
  before any Phase-1/Phase-2 heuristic lookup (index depends on `sym`).
  Sizes: 65.8 MB + 5.4 MB.

Recommendation (pre-removal, superseded): P1 was to validate `cornerHeuristicHTM`
and `edgePermutationHeuristicHTM` as extra `max()` heuristics for the
single-phase solver, if index-mapping + admissibility tests pass.
`kociembaPhase1HeuristicHTM` (+Phase1 Movement + Phase1 Symmetry) was the
second candidate. Two-phase tables were future-work then; `twophase` now
wires them in.

## 5. File-format verification plan (must pass before any solver use)

All checks are read-only vs `tables\` + reproducible from `data\` + BFS.

1. **BFS depth ≤6 admissibility (heuristic ≤ true distance).**
   For each candidate heuristic `h` (corner HTM, edgePerm HTM, phase1 HTM),
   BFS from solved over our 18 HTM moves to depth 6 (3,901,490 nodes max:
   18^6 with pruning — batch via `solver.cpp` successors), compute true
   distance per index, assert `h(index) ≤ true` for every visited pattern.
   If Phase-2 tables are ever used, repeat with BFS restricted inside G1
   (10 phase-2 moves) to depth 6.

2. **Entry-by-entry 1M random indices vs `data\`.**
   Sample 1,000,000 uniform indexes in `[0,88179840)` (corner) and compare
   `tables\cornerHeuristicHTM` nibble vs the value predicted by
   reimplementing upstream `encodeAllCorners` + upstream index order
   applied to a generated state (not vs `data\corner.db` byte-for-byte —
   rankings differ, so compare *distance values*, not raw bytes).
   Similarly 1M random 12!-ranks vs a fresh BFS for edge permutation.
   Goal: catch packing (low/high nibble) and orientation-digit-order bugs.
   Also repeat for Phase-1: sample `(twist,flip,class45)` uniformly in
   reduced domain, compare nibble vs BFS distance to G1 to depth ≤6.

3. **Length parity / depth ≤11 consistency.**
   For 1,000 random HTM scrambles of depth ≤10, solve with
   `twophase::solve` (with and without the `tables\` HTM heuristics);
   assert solutions solve (reference simulator) and lengths match
   (the heuristic only tightens IDA* bounds). Any `DatabaseMissing`
   vs. new-table fallback must be distinguished (a missing `tables\` file
   must not silently fall back to a degraded search).

4. **Fallback test.**
   Point loader at empty dir / missing `tables\` file / wrong size /
   wrong hash; assert `DatabaseMissing` (no crash, no partial heuristic).
   Then point at correct `tables\` set; assert solves resume. Hash-check
   loader (SHA-256 per file) must fail closed on mismatch.

5. **Movement-table consistency.**
   For each movement file, spot-check 10,000 random `(index,move)`:
   apply move via `apply()`/`applyTurnToPieceCube` equivalent, re-encode
   coordinate, assert equals stored successor. Repeat for PieceCube
   single-cubie tables (32/24-byte components).

Acceptance: all five pass; `ctest` green with zero warnings; no `tables\`
file modified.

## 6. Origin / licence

- **Origin (verified):** every SHA-256 in §1.1 matches the public table
  page verbatim — James Cameron, "Optimal Rubik's Cube Solver" (a proper
  name for the upstream project, not this repo's former method),
  `https://jamescameron.me/rubiks-cube-solver` / tables page
  `https://jamescameron.me/rubiks-cube-solver/tables`, source repo
  `https://github.com/Equinaut/Optimal-Rubiks-Cube-Solver` (C, IDA*
  with Korf/Reid heuristics + Kociemba two-phase, 4 metrics HTM/QTM/STM/ATM).
  Reference docs: `docs/lookup-tables.md` ("heuristic / movement / symmetry"
  tables), `src/lookupTables/*` (generation + on-disk layout), `src/encode/*`
  (index functions), `src/common.h` (move counts). Table bytes here are
  bit-identical to upstream downloads (hashes confirm).
- **Licence: unknown** — no LICENSE file in upstream repo root (listing shows
  only `Makefile, README.md, docs/, src/, random100.txt, .gitignore`).
  Do not treat as permissive. Do not copy upstream code into this repo;
  reimplement index functions from the documented mathematics (Kociemba
  two-phase coordinates, Korf 1997) with citations, or seek permission
  before distributing derived tables. Internal use only until then.

Note: this origin was *not* assumed — it was located by searching the
exact filenames + hashes (read-only `websearch`/`webfetch`), then
confirming byte-for-byte hashes locally (§1.1 vs upstream page).

## 7. Constraints & portability (plan only, no code yet)

- **Frozen interface unchanged.** `docs/SPEC.md` §8 signature
  (`solve(CubeState,SolveOptions) → expected<Solution,SolveError>`) and
  `Solver`/`SolveOptions` are untouched. Any `tables\` heuristics are
  internal to `src/solvers/twophase/` (extra `h` terms). No new
  `SolveOptions` fields, no new `SolveError` values.
- **No new deps.** Loader uses only C++23 stdlib (`<filesystem>`,
  `<fstream>`, `<vector>`, `<expected>`). No GoogleTest additions, no
  compression libs, no mmap libraries.
- **Portability note for loader (plan only):**
  - Preferred: Win32 `CreateFileMapping`/`MapViewOfFile` + `VirtualAlloc`
    equivalent, virtual size = exact file size (no header), read-only
    (`FILE_MAP_READ`, `PAGE_READONLY`) on Windows/MSYS2 UCRT64.
  - Fallback (POSIX/MinGW, and CI): buffered `std::ifstream` read of the
    full file into `vector<uint8_t>` (44 MB / 240 MB / 100 MB are fine;
    2.4 GB `large*` needs streaming/mapping — defer).
  - Fallback-2: memory-map via `std::mapped_file` if available (not in
    standard C++23 — guard with `#ifdef` and default to buffered read).
  - All loaders verify `file_size == expected_component_bytes` and
    optionally SHA-256 (requires `<openssl>`-free SHA-256 implementation
    inline — ~80 lines, no dep) before exposing pointers. Missing/mismatch
    → `DatabaseMissing`.
- **`data\` vs `tables\`:** keep separate. Our `data\*.db` 40-byte header
  format stays as-is. `tables\` files are headerless; loader must branch
  on file magic (`RPDB` vs raw) — do not add headers to `tables\` copies,
  do not write into `tables\`.
- **`tables\` untouched.** This recon did not write, rename, or delete
  inside `tables\` (verified by hashes above matching upstream). Writable
  path is this file only.

## Appendix A — Commands run (Phase 0, read-only)

- `Get-ChildItem tables -File | Sort-Object Name | Format-Table Name,Length` (stat sizes)
- `Get-ChildItem data -File | Format-Table Name,Length`
- `Get-ChildItem tables -File | Sort-Object Name | Get-FileHash -Algorithm SHA256`
- `Get-ChildItem data -File | Get-FileHash -Algorithm SHA256`
- First-64 bytes per file: `File.OpenRead` + `Read(buf,0,64)` → hex join
  (`tables\` 34 files + `data\` 4 files)
- Arithmetic checks via powershell (`$p1=100776960; $a=$p1*2; …/2187…/2048…`)
  to factor sizes against 2187/2048/495/40320/12! (identifying the 45 and
  2768 reduction factors before source confirmation).
- Read-only web: `websearch` (filenames+hash → upstream page),
  `webfetch` (tables page, solver page, GitHub README, `docs/lookup-tables.md`,
  `src/lookupTables/*`, `src/encode/*`, `src/common.h` via
  `api.github.com`/`raw.githubusercontent.com`). No external writes,
  no credentials.

## Appendix B — Open issues (for main-agent approval before P1)

1. **Cubie-order map unfrozen.** Need a one-page spec of
   `cube.hpp` (CP/CO/EP/EO) ↔ upstream (UBL-keyed corners, UB-keyed edges,
   `unorder=[1,5,0,4,3,7,2,6]`) including orientation digit orders.
   Without it, every §3.2 index is untestable.
2. **HTM-only vs multi-metric.** Frozen solver is HTM-18-face-moves.
   Only HTM tables are admissible. Decision needed: load HTM set only
   (4.3 GB → HTM subset ≈ corner 44 MB + edgePerm 240 MB + phase1 100 MB +
   movements ~7 MB + symmetry 71 MB ≈ 462 MB) or defer all ATM/QTM/STM.
3. **`largeKociembaPhase1HeuristicHTM` 2.4 GB** — RAM/disk budget and
   mapping strategy need decision before loading. Recommend excluding
   from P1.
4. **Licence unknown** (see §6). Reuse of `tables\` bytes is read-only
   recon; productizing heuristics derived from them needs licence clarity.
5. **Two-phase solver** was deferred then (a new method outside the four
   human methods); it has since been added (`src/solvers/twophase/`) without
   changing the frozen `solve()` signature.
6. **`kociembaPhase1Symmetry` 65 MB `u32` tail** includes
   `1,013,760×16` edge-orientation symmetries — verify memory fit on this
   machine (upstream states 3 GB RAM for full set) before adding.

## Approval (user 2026-11-08)

User approved **All incl. two-phase**: integrate cornerHTM+edgePerm+phase1 as extra max() heuristics for the single-phase solver (HTM+all metrics), large (2.4GB, memory-mapped), and phase2+symmetry+movements as a **new two-phase solver method** (explicitly approved new method). Licence remains listed as **unknown** in this file (no upstream LICENSE); loader will fail-closed on size/hash. (The single-phase solver was later removed; see removal note at top.)
Encoding utf8
