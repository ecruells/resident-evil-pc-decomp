# Changelog

## [1.4.2](https://github.com/ecruells/resident-evil-pc-decomp/compare/residentevil-v1.4.1...residentevil-v1.4.2) (2026-11-04)


### Bug Fixes

* crash when a death by a zombie ocurrs in a room with a mirror ([f8b97b9](https://github.com/ecruells/resident-evil-pc-decomp/commit/f8b97b98fdb70c7ca0314461a1fd0fbd5154626f))
* game crash when head shooting many enemies in a row ([aaeb2a0](https://github.com/ecruells/resident-evil-pc-decomp/commit/aaeb2a0c63cb6d3b5af5c205e8c124cec63dcad4))
* migrated ps1 and saturn bgms starts with the loops end ([5f28186](https://github.com/ecruells/resident-evil-pc-decomp/commit/5f28186bea5c2d3af4a9305f4fe8aba59a96c30e))
* **saturn:** glitched rooms backgrounds in some rooms ([67e88e9](https://github.com/ecruells/resident-evil-pc-decomp/commit/67e88e9120d06f0784a8aff4fb82aaee60177204))
* some custom emd models have transparent colors instead of solid ([fd8a2e0](https://github.com/ecruells/resident-evil-pc-decomp/commit/fd8a2e031a08ee435c545fa675caa951c22b9ae8))
* **textures:** missing hd textures and hd tex load errors in dc and saturn modes ([dbb61d1](https://github.com/ecruells/resident-evil-pc-decomp/commit/dbb61d18fcbe63deee7b9326ae62fd8a424bed1b))
* white dot appearing in dining room main door ([907c23c](https://github.com/ecruells/resident-evil-pc-decomp/commit/907c23c8b5ec243ab812e34ddf1363da9d709a27))
* yawn2 floor smash effects looks like an explosion ([d430af9](https://github.com/ecruells/resident-evil-pc-decomp/commit/d430af9cf0e413474918753dbb3ceb863ca1ccbc))

## [1.4.1](https://github.com/ecruells/resident-evil-pc-decomp/compare/residentevil-v1.4.0...residentevil-v1.4.1) (2026-10-23)


### Bug Fixes

* dc mode is forcing western difficulty by default ([9c47820](https://github.com/ecruells/resident-evil-pc-decomp/commit/9c47820d956279ba2f9ea356bcaa3422d9dc9781))
* **gui:** cannot go to the desktop doing alt-tab ([8fea9af](https://github.com/ecruells/resident-evil-pc-decomp/commit/8fea9afcc6636e519c77a12deebcbce0cf198d3a))
* **saturn:** Second tyrant theme playing from looping point instead from the start ([41e9887](https://github.com/ecruells/resident-evil-pc-decomp/commit/41e988742408c9b61bf52f802e13217ba774440b))

## [1.4.0](https://github.com/ecruells/resident-evil-pc-decomp/compare/residentevil-v1.3.0...residentevil-v1.4.0) (2026-10-23)


### Features

* added game options men ([17d5a1a](https://github.com/ecruells/resident-evil-pc-decomp/commit/17d5a1a42e182243e613ef2fc6439d64629bff32))
* added jap saturn version support ([8922aa0](https://github.com/ecruells/resident-evil-pc-decomp/commit/8922aa0b440dd2974fa9675fe3eb89320ef631b8))
* added saturn mode flag in save game ([baf672b](https://github.com/ecruells/resident-evil-pc-decomp/commit/baf672be379fc254421ec07fb8dbc45db1fac711))
* added saturns fmvs migration ([81a04bc](https://github.com/ecruells/resident-evil-pc-decomp/commit/81a04bc1361cd4d4cce702cc5bd273ea9b20b9ba))
* allow play battle game in OG and DC modes ([af2b6ac](https://github.com/ecruells/resident-evil-pc-decomp/commit/af2b6ac64dbdbc9514b78e71c3395680f3635d2d))
* allow to migrate all rooms, bgs and bgms in saturns migrator ([ee1be11](https://github.com/ecruells/resident-evil-pc-decomp/commit/ee1be112b3b0506a5b128fa7aa87c10bc26ee175))
* allow to migrate all saturn characters models from migrator ([e33f9a9](https://github.com/ecruells/resident-evil-pc-decomp/commit/e33f9a9e40d038c6b3499d950929634b8b334a44))
* **bump:** version bump ([9cdb25d](https://github.com/ecruells/resident-evil-pc-decomp/commit/9cdb25d6be7bca779a90aaea75eb3c0442e65b65))
* expand save slots and display game mode and timestamp description for selected slot ([ac4dd45](https://github.com/ecruells/resident-evil-pc-decomp/commit/ac4dd455b3b949325ef3b7ef819b2f5c70990b68))
* hd textures and videos support added ([b7cfd06](https://github.com/ecruells/resident-evil-pc-decomp/commit/b7cfd068280af67cf9f74ccbecadeff3f3057b7e))
* implemented full Tick behaviour code ([a86b009](https://github.com/ecruells/resident-evil-pc-decomp/commit/a86b009146d0d3c1c14f1f76a7f8796ae57098cd))
* implemented western difficulty for DC and Saturn modes, optional for PC OG ([5158428](https://github.com/ecruells/resident-evil-pc-decomp/commit/515842801deea0c821332949ef7a5dc07d5dc99f))
* restore ps1 alpha blending for effects like glass and smoke ([852ffdd](https://github.com/ecruells/resident-evil-pc-decomp/commit/852ffdd43e5b588a41b8ce359bae3c3742ccbd68))
* saturn version port implemented ([e19a8f6](https://github.com/ecruells/resident-evil-pc-decomp/commit/e19a8f6734ba0ff3ccc4cec55b40f1c70fd89760))


### Bug Fixes

* assets migrator build error with gcc16 in linux ([84f4233](https://github.com/ecruells/resident-evil-pc-decomp/commit/84f4233239f11439f782afd5f6155fd2a8187996))
* audio resets when game gains focus after losing it ([66d8147](https://github.com/ecruells/resident-evil-pc-decomp/commit/66d814774d25080d809214aa14afdea95c6e8227))
* bad HD texture for models with no HD texture present ([0f6342f](https://github.com/ecruells/resident-evil-pc-decomp/commit/0f6342f2ef299680e3b924b725d5494d5f962769))
* battle game clock keeps running when arena is cleared and paused then inventory/itembox is open ([9a39607](https://github.com/ecruells/resident-evil-pc-decomp/commit/9a39607ccd5347a23adac6f1010690f9a77d7bf5))
* battle game timer 2x slower ([cb9090d](https://github.com/ecruells/resident-evil-pc-decomp/commit/cb9090da797aebf32e7eca1259ff2f266a56cda7))
* beheaded corpse always in first death of a game session in the YOU DIE screen ([2110064](https://github.com/ecruells/resident-evil-pc-decomp/commit/21100642578b695f452a4e43204beaaca0252c3c))
* death screen text waving effect different than original game ([d91d056](https://github.com/ecruells/resident-evil-pc-decomp/commit/d91d05686942ae1cb3daf2e3535d347e212661dc))
* enter water gate room playing as jill in linux version makes the game crash ([3af25c5](https://github.com/ecruells/resident-evil-pc-decomp/commit/3af25c5c9ab14b98c928f9b66172374e66b32b2a))
* game option menu opening with inventory key ([0a54933](https://github.com/ecruells/resident-evil-pc-decomp/commit/0a54933c8837f10a9c5cf4226aa284c56cc16d35))
* invisible 3d objects on arch linux ([bbff159](https://github.com/ecruells/resident-evil-pc-decomp/commit/bbff159f3299f5fc9e55e3328420b244083f7baa))
* jills prologue fmv cut delayed in censored versions ([24df8c7](https://github.com/ecruells/resident-evil-pc-decomp/commit/24df8c795b6df2964e0119c5d09d39b70f4899e6))
* kicked zombie head looks squashed ([c6104cc](https://github.com/ecruells/resident-evil-pc-decomp/commit/c6104cca9fd0846837de08f38b3d8971c54fec4b))
* make jill need inks ribbons in jpn saturn and dc modes ([22cbecc](https://github.com/ecruells/resident-evil-pc-decomp/commit/22cbeccfc744d11a2388be1239bc83794383ceee))
* missing ps1 sounds from migrator ([ff19dc3](https://github.com/ecruells/resident-evil-pc-decomp/commit/ff19dc3feb5b2b0eea9646cfa7966673e5a51d45))
* no ammo stack up when inventory is full ([3285aa6](https://github.com/ecruells/resident-evil-pc-decomp/commit/3285aa6761fe4e8b5b21b6d6df9e9d1a8753abf5))
* patch saturns wardrobe to allow exclusive outfit change from RDT ([1ccbbe0](https://github.com/ecruells/resident-evil-pc-decomp/commit/1ccbbe00004432eff48c72f98e111a9a6aa9a70d))
* saturn main lab, jill scene plays chris scene instead ([dadd74e](https://github.com/ecruells/resident-evil-pc-decomp/commit/dadd74eadd1a4089448d7e127b8b318313b58e62))
* shoot camera effect trigger without aiming the camera and implement DC effect duration fix ([7fe9280](https://github.com/ecruells/resident-evil-pc-decomp/commit/7fe92801f916259a9fd2a4fc8eed60259cc87a40))
* spiders can still poison you in battle game ([a874bcd](https://github.com/ecruells/resident-evil-pc-decomp/commit/a874bcd1a4b9f2963177987f75f9abfef0401688))
* swapped map and wrong sfx issues; implemented ps1 pixel blending for maps rendering ([10e1d10](https://github.com/ecruells/resident-evil-pc-decomp/commit/10e1d109b52de0be43c1d18acd10484e30b699a7))
* tyrant blasted limbs are not black and has wrong positions ([ad284a0](https://github.com/ecruells/resident-evil-pc-decomp/commit/ad284a0adafe62ac66123c5e00723fcb778a7d03))
* wobling healthbar in battle game scoring screen ([c6a355e](https://github.com/ecruells/resident-evil-pc-decomp/commit/c6a355ea3af90dc2bf48f5c27d2099d43a74e6d4))
* yawn ai bug in battle game ([fb6b7fc](https://github.com/ecruells/resident-evil-pc-decomp/commit/fb6b7fc45454cf889d9e29621a9dc2a3edebca7a))
* zombies in room 707 instead of hunters when using debug menu room change ([15aab70](https://github.com/ecruells/resident-evil-pc-decomp/commit/15aab707dd196d31c69902138f74d5412b49719f))

## [1.3.0](https://github.com/ecruells/resident-evil-pc-decomp/compare/residentevil-v1.2.0...residentevil-v1.3.0) (2026-09-27)


### Features

* added ogg audio support and ps1 audio migration ([47c653c](https://github.com/ecruells/resident-evil-pc-decomp/commit/47c653cae14e52088f97d0c471e7fa8857085949))
* added ps1 far object culling ([758cb3b](https://github.com/ecruells/resident-evil-pc-decomp/commit/758cb3b7a1e96005024b83fbdf060062c22aba98))
* added skip unskippable fmvs game flag ([8e78091](https://github.com/ecruells/resident-evil-pc-decomp/commit/8e780918dd3f31e096f083a64d23d597808fef92))
* asset migration tool added ([17ecd2c](https://github.com/ecruells/resident-evil-pc-decomp/commit/17ecd2cb73625d6402d31589eda548896294a34a))
* implemented directors cut mode support ([b189829](https://github.com/ecruells/resident-evil-pc-decomp/commit/b189829015eb5433d447382224a84974ca7c2549))
* implemented mp4 video playback ([298dd14](https://github.com/ecruells/resident-evil-pc-decomp/commit/298dd14f5c03300ef0345284dc9361224fe11954))
* port PS1 credit roll overlay system ([0abfc5a](https://github.com/ecruells/resident-evil-pc-decomp/commit/0abfc5a78e96cf72a37d7c0e32c3fdff8cc7cc90))
* port Ps1 Japanese prologue fmv subtitle subsystem ([f52286b](https://github.com/ecruells/resident-evil-pc-decomp/commit/f52286bae4b5b6aa8fa06525f3454e77db302a56))
* update dc migration to only import exclusive assets not present in PC ([a7c5835](https://github.com/ecruells/resident-evil-pc-decomp/commit/a7c5835784e78176c23c70f975ecfe3eaea079bb))


### Bug Fixes

* 2f mansion map bug regression ([72cf8f6](https://github.com/ecruells/resident-evil-pc-decomp/commit/72cf8f6eb611afd951c1cac3508e283b154a2288))
* asset migrator build error ([4d5a29d](https://github.com/ecruells/resident-evil-pc-decomp/commit/4d5a29d88a1f35c7037b55290211ac72f50b0b51))
* asset migrator build error ([8ec9df0](https://github.com/ecruells/resident-evil-pc-decomp/commit/8ec9df00404706be079cf1505851ae5b01342f6c))
* items test using ascii codes instead of items id constants ([9f286d6](https://github.com/ecruells/resident-evil-pc-decomp/commit/9f286d6a3efa903e75ec7cf082d6d28ca352f69c))
* JPN version using wrong title sound bank ([cdb5046](https://github.com/ecruells/resident-evil-pc-decomp/commit/cdb5046e4fb5f3db1d5e269f2c57ccebbbb61c42))
* masking issues in outfit change transition for arrange outfit change ([9131ed2](https://github.com/ecruells/resident-evil-pc-decomp/commit/9131ed2a8dbbe3ee73dcb6e8370ed0004c5451ee))
* no beretta custom in saved new game after ending an advanced run ([5a8809f](https://github.com/ecruells/resident-evil-pc-decomp/commit/5a8809f745322bd3d24ece684263b3663ae8da15))
* offset and bad glyphs in ending result screen values for JPN version ([c251ddd](https://github.com/ecruells/resident-evil-pc-decomp/commit/c251dddbcee96889d31b62242a75e1c9c6612dcd))
* str to mp4 timing issues ([ace7967](https://github.com/ecruells/resident-evil-pc-decomp/commit/ace79670757a86fbe7d1e0e7061d0ec1e595ca8f))
* videos playback and DC mode path error on linux ([fb9cfcc](https://github.com/ecruells/resident-evil-pc-decomp/commit/fb9cfcc8505f6cd336bd4ababb59665e03d4ce52))
* wrong ingram and minimi names and descriptions in DC mode ([7fa097c](https://github.com/ecruells/resident-evil-pc-decomp/commit/7fa097cb7c1b45ec9cedf216f95a2df2633d6949))

## [1.2.0](https://github.com/ecruells/resident-evil-pc-decomp/compare/residentevil-v1.1.1...residentevil-v1.2.0) (2026-09-10)


### Features

* added JPN version F9 menu texts ([fc0b0dc](https://github.com/ecruells/resident-evil-pc-decomp/commit/fc0b0dc1724467107c700bdf776cfd43a809069d))


### Bug Fixes

* implement JPN file reader exclusive background images ([867f6c9](https://github.com/ecruells/resident-evil-pc-decomp/commit/867f6c9e68bb7c43b21b5cb2a64403d92f4b0d36))

## [1.1.1](https://github.com/ecruells/resident-evil-pc-decomp/compare/residentevil-v1.1.0...residentevil-v1.1.1) (2026-09-10)


### Bug Fixes

* rename linux package to tar.gz ([e8f44e1](https://github.com/ecruells/resident-evil-pc-decomp/commit/e8f44e157b0e620b9f198ce2fa86d688bc98e6d0))

## [1.1.0](https://github.com/ecruells/resident-evil-pc-decomp/compare/residentevil-v1.0.1...residentevil-v1.1.0) (2026-09-10)


### Features

* linux ci fix ([394f588](https://github.com/ecruells/resident-evil-pc-decomp/commit/394f588f92c4139fe6ecdbac57ff336558fcc1d5))
* linux port ([178bffe](https://github.com/ecruells/resident-evil-pc-decomp/commit/178bffe5db0bcd6b5159cd464743466608ba7f52))
* update release ci to add linux build ([1232d0a](https://github.com/ecruells/resident-evil-pc-decomp/commit/1232d0a5141946a769b630bf49bb930dbd916776))

## [1.0.1](https://github.com/ecruells/resident-evil-pc-decomp/compare/residentevil-v1.0.0...residentevil-v1.0.1) (2026-09-08)


### Bug Fixes

* bug that breaks character state doing quick load between different characters ([e962857](https://github.com/ecruells/resident-evil-pc-decomp/commit/e962857ceb6597537034281062cdc83668d875ce))
* bug that makes chimeras warp when you shot them and fall from ceiling ([cb11a87](https://github.com/ecruells/resident-evil-pc-decomp/commit/cb11a87c756757aa71834ecddce066a201b7f698))
* debug menu off-screen and bad contrast texts ([dbc62ba](https://github.com/ecruells/resident-evil-pc-decomp/commit/dbc62bae69d70859925a2107e8ec9eda68985ead))
* misplacement when changing rooms in certains rooms ([fe98b42](https://github.com/ecruells/resident-evil-pc-decomp/commit/fe98b42c2cef76d0a63ce0ab2964562215383af3))
* missing jills hand animation in computer lab subsystem ([6d0cbb9](https://github.com/ecruells/resident-evil-pc-decomp/commit/6d0cbb9aa7e3ccae7c824be067a3a9188e5bf234))
* shadows drawing above bg masks ([80c924e](https://github.com/ecruells/resident-evil-pc-decomp/commit/80c924e16003db0f8736981a054a6ca9d87a4cf3))

## 1.0.0 (2026-09-08)


### Features

* add release workflow with release-please versioning ([2ffc181](https://github.com/ecruells/resident-evil-pc-decomp/commit/2ffc1813051c0616288a174baa3fe6737c6ff028))
* added jap saveload menu strings and layout ([7d46c59](https://github.com/ecruells/resident-evil-pc-decomp/commit/7d46c596720c927d8a7da14ad76561e0e4b1db41))
* added support for japanese text encoding and globals texts ([9ffe0bd](https://github.com/ecruells/resident-evil-pc-decomp/commit/9ffe0bd28abfb9493d1550935b7f2a0cf1dc8d49))
* implement debug menu in the spirit of classic rebirth one ([2c83e4f](https://github.com/ecruells/resident-evil-pc-decomp/commit/2c83e4f4f8de0f733f27478759e099d85daff806))
* implement xinput support ([e994317](https://github.com/ecruells/resident-evil-pc-decomp/commit/e994317d6bd9a122684936469f6ee26f87b7f062))


### Bug Fixes

* 2f map canvas not fading out when use lighter ([dace6db](https://github.com/ecruells/resident-evil-pc-decomp/commit/dace6db6d1431b89032a8c1ba7686bbfc93b13fb))
* add missing emd ([4bc18bb](https://github.com/ecruells/resident-evil-pc-decomp/commit/4bc18bbe12c7d7dab76453ee90d44d6daa988f14))
* crash opening the options menu in release build ([14b9e08](https://github.com/ecruells/resident-evil-pc-decomp/commit/14b9e08fb277ca4ba7f36532fb643be526484a2c))
* events not waiting voice playing end ([e2a87ef](https://github.com/ecruells/resident-evil-pc-decomp/commit/e2a87ef170935a1f3f018b283b8129d5a3ca8ea4))
* incorrect itembox list frame elements positions ([d40dece](https://github.com/ecruells/resident-evil-pc-decomp/commit/d40dece73c9a815fb3b077f720379272595a9afd))
* incorrect minimi aim angles ([e259700](https://github.com/ecruells/resident-evil-pc-decomp/commit/e2597009f48635a4e60e1164cf01f54ffa5f3470))
* missing swallowed animation ([eb73481](https://github.com/ecruells/resident-evil-pc-decomp/commit/eb734814303f29455896139359b5d8ff4f277128))
* no blood effect in hunter slashing rebecca ([01cd0b3](https://github.com/ecruells/resident-evil-pc-decomp/commit/01cd0b3f7dfa91d5e4da009470f24e4e7b0599e8))
* no gory explosion effect rendering ([4e6d066](https://github.com/ecruells/resident-evil-pc-decomp/commit/4e6d0667a396ddea6a8a29b91a88ba7fd8baf214))
* no voices audio in underground barrys death scene ([f134762](https://github.com/ecruells/resident-evil-pc-decomp/commit/f134762f58d00d178f809ff657db6b276133ff91))
* odd web spinner behaviour ([5e25675](https://github.com/ecruells/resident-evil-pc-decomp/commit/5e256753e833d777a9984f38e7825c7cff6375be))
* passcode panels light always red colored when passcode inserted instead of blue ([5725961](https://github.com/ecruells/resident-evil-pc-decomp/commit/5725961b8abce280a1ace1d7607b20cce8b1f018))
* regenerated all 896 bytes of the table for effect sprites ([cde96c4](https://github.com/ecruells/resident-evil-pc-decomp/commit/cde96c436a787f80c6a9e61dd92f18344fe8a7e4))
* remove sparkle effect when picking item ([a419b98](https://github.com/ecruells/resident-evil-pc-decomp/commit/a419b980c0fe60385cca86abb9ce4de8c763145d))
* rename misnormer depth to texturePage ([020e2b5](https://github.com/ecruells/resident-evil-pc-decomp/commit/020e2b589b671053d62cf4b716576f6cb116eeb8))
* texture bankid and cell bug ([a5b25c8](https://github.com/ecruells/resident-evil-pc-decomp/commit/a5b25c8271dfc244ab9a7208aff9951e8249161e))
* textureDesc misnormer properties ([56dceda](https://github.com/ecruells/resident-evil-pc-decomp/commit/56dceda56b721c2b2b00cedb8b83d7221d34f47b))
* wasp red blood effect bug ([db328d9](https://github.com/ecruells/resident-evil-pc-decomp/commit/db328d9683c8fb2379315e5e6abec5dc6debc679))
* water tank transparent in study 2f ([288468b](https://github.com/ecruells/resident-evil-pc-decomp/commit/288468b6db59bec12e46282c0bc3af2a3cdc0783))
* wrong sweep color when healing ([922e344](https://github.com/ecruells/resident-evil-pc-decomp/commit/922e344eacff9dcf1e5a2a2e8e057146cc1ead1f))
* wrong weapons reload anim and implement missing falling magazine anim ([5e91705](https://github.com/ecruells/resident-evil-pc-decomp/commit/5e917050270ec95763af67837a344cb40a55f819))
* zombies vomit not rendering ([3644404](https://github.com/ecruells/resident-evil-pc-decomp/commit/364440465b83eaddd114e80e4d16c362f6ba994a))
