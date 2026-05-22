# Module: (curry tts)

*v0.8.17 — 2026-05-22*

Text-to-speech synthesis via **eSpeak NG** (`libespeak-ng`).
eSpeak NG is a compact, open-source formant synthesiser supporting 141 languages.
It runs entirely on the local CPU — no network, no API key.

## Installation

```bash
# macOS
brew install espeak-ng

# Debian / Ubuntu
sudo apt install libespeak-ng-dev espeak-ng-data
```

Enable at build time:

```bash
cmake -B build -DBUILD_MODULE_TTS=ON
cmake --build build
```

## Import

```scheme
(import (curry tts))
```

## Speaking text

```scheme
(tts-speak text)              ; synchronous playback (blocks until audio completes)
(tts-speak text lang)         ; set voice/language for the session, then speak
```

`tts-speak` uses `AUDIO_OUTPUT_SYNCH_PLAYBACK` and does not return until the
audio has finished playing through the system audio device.

The optional `lang` argument accepts either a full voice name
(as returned by `tts-voices`) or a language identifier (as returned by
`tts-voice-table`), e.g. `"en"`, `"fr"`, `"de"`, `"gmw/en-GB-x-rp"`.

```scheme
(tts-speak "Hello, world!")
(tts-speak "Bonjour le monde" "fr")
(tts-speak "Guten Morgen" "de")
(tts-speak "こんにちは" "jpx/ja")
```

## PCM retrieval

```scheme
(tts->pcm text)  ; -> bytevector of 16-bit little-endian PCM samples
```

Synthesises `text` using `AUDIO_OUTPUT_SYNCHRONOUS` (no hardware playback)
and returns the raw audio as a bytevector. The sample rate is 22050 Hz
(verify with `tts-sample-rate`). Each sample is two bytes, little-endian signed.

```scheme
(define pcm (tts->pcm "hello"))
(bytevector-length pcm)          ; -> 18630  (9315 samples ≈ 0.42 s at 22050 Hz)
```

Calling `tts->pcm` after `tts-speak` (or vice versa) reinitialises the
eSpeak backend transparently; all parameter settings are preserved.

## Parameters

```scheme
(tts-set-rate!   wpm)   ; speaking rate in words per minute  (80–450; default 175)
(tts-set-pitch!  n)     ; base pitch                         (0–100;  default 50)
(tts-set-volume! n)     ; output volume                      (0–200;  default 100)
(tts-set-voice!  name)  ; voice name or language identifier
```

Changes take effect immediately if eSpeak is already initialised.
Settings are preserved across `tts-speak` / `tts->pcm` mode switches.

Constants:

```scheme
tts-rate-min     ; 80
tts-rate-max     ; 450
tts-rate-normal  ; 175
```

## Sample rate

```scheme
(tts-sample-rate)   ; -> integer Hz (22050 for a standard eSpeak NG install)
```

## Voice discovery

```scheme
(tts-voices)         ; -> list of human-readable name strings (141 entries)
(tts-voice-table)    ; -> list of (name . identifier) pairs
```

Use `tts-voice-table` to find the identifier to pass to `tts-set-voice!` or the
`lang` argument of `tts-speak`:

```scheme
(assoc "French (France)" (tts-voice-table))
; => ("French (France)" . "roa/fr")

(tts-speak "Vive la liberté" "roa/fr")
```

## Complete voice table

141 voices as of eSpeak NG 1.51 (Homebrew / standard Linux package).
The **Identifier** column is the value to pass to `tts-set-voice!` or the `lang`
argument of `tts-speak`.

| Name | Identifier |
|------|-----------|
| Afrikaans | `gmw/af` |
| Amharic | `sem/am` |
| Aragonese | `roa/an` |
| Arabic | `sem/ar` |
| Assamese | `inc/as` |
| Azerbaijani | `trk/az` |
| Bashkir | `trk/ba` |
| Belarusian | `zle/be` |
| Bulgarian | `zls/bg` |
| Bengali | `inc/bn` |
| Bishnupriya Manipuri | `inc/bpy` |
| Bosnian | `zls/bs` |
| Catalan | `roa/ca` |
| Catalan (Balearic) | `roa/ca-ba` |
| Catalan (North-western) | `roa/ca-nw` |
| Catalan (Valencian) | `roa/ca-va` |
| Cherokee | `iro/chr` |
| Chinese (Mandarin, latin as English) | `sit/cmn` |
| Chinese (Mandarin, latin as Pinyin) | `sit/cmn-Latn-pinyin` |
| Czech | `zlw/cs` |
| Chuvash | `trk/cv` |
| Welsh | `cel/cy` |
| Danish | `gmq/da` |
| German | `gmw/de` |
| Greek | `grk/el` |
| English (Caribbean) | `gmw/en-029` |
| English (Great Britain) | `gmw/en` |
| English (Scotland) | `gmw/en-GB-scotland` |
| English (Lancaster) | `gmw/en-GB-x-gbclan` |
| English (West Midlands) | `gmw/en-GB-x-gbcwmd` |
| English (Received Pronunciation) | `gmw/en-GB-x-rp` |
| English (Shavian alphabet) | `gmw/en-Shaw` |
| English (America) | `gmw/en-US` |
| English (America, New York City) | `gmw/en-US-nyc` |
| Esperanto | `art/eo` |
| Spanish (Spain) | `roa/es` |
| Spanish (Latin America) | `roa/es-419` |
| Estonian | `urj/et` |
| Basque | `eu` |
| Persian | `ira/fa` |
| Persian (Pinglish) | `ira/fa-Latn` |
| Finnish | `urj/fi` |
| Faroese | `gmq/fo` |
| French (Belgium) | `roa/fr-BE` |
| French (Switzerland) | `roa/fr-CH` |
| French (France) | `roa/fr` |
| Gaelic (Irish) | `cel/ga` |
| Gaelic (Scottish) | `cel/gd` |
| Guarani | `sai/gn` |
| Greek (Ancient) | `grk/grc` |
| Gujarati | `inc/gu` |
| Hakka Chinese | `sit/hak` |
| Hawaiian | `map/haw` |
| Hebrew | `sem/he` |
| Hindi | `inc/hi` |
| Croatian | `zls/hr` |
| Haitian Creole | `roa/ht` |
| Hungarian | `urj/hu` |
| Armenian (East Armenia) | `ine/hy` |
| Armenian (West Armenia) | `ine/hyw` |
| Interlingua | `art/ia` |
| Indonesian | `poz/id` |
| Ido | `art/io` |
| Icelandic | `gmq/is` |
| Italian | `roa/it` |
| Japanese | `jpx/ja` |
| Lojban | `art/jbo` |
| Georgian | `ccs/ka` |
| Karakalpak | `trk/kaa` |
| Kazakh | `trk/kk` |
| Greenlandic | `esx/kl` |
| Kannada | `dra/kn` |
| Korean | `ko` |
| Konkani | `inc/kok` |
| Kurdish | `ira/ku` |
| Kyrgyz | `trk/ky` |
| Latin | `itc/la` |
| Luxembourgish | `gmw/lb` |
| Lingua Franca Nova | `art/lfn` |
| Lithuanian | `bat/lt` |
| Latgalian | `bat/ltg` |
| Latvian | `bat/lv` |
| Māori | `poz/mi` |
| Macedonian | `zls/mk` |
| Malayalam | `dra/ml` |
| Marathi | `inc/mr` |
| Malay | `poz/ms` |
| Maltese | `sem/mt` |
| Totontepec Mixe | `miz/mto` |
| Myanmar (Burmese) | `sit/my` |
| Norwegian Bokmål | `gmq/nb` |
| Nahuatl (Classical) | `azc/nci` |
| Nepali | `inc/ne` |
| Dutch | `gmw/nl` |
| Nogai | `trk/nog` |
| Oromo | `cus/om` |
| Oriya | `inc/or` |
| Punjabi | `inc/pa` |
| Papiamento | `roa/pap` |
| Klingon | `art/piqd` |
| Polish | `zlw/pl` |
| Portuguese (Portugal) | `roa/pt` |
| Portuguese (Brazil) | `roa/pt-BR` |
| Pyash | `art/py` |
| Lang Belta | `art/qdb` |
| Quechua | `qu` |
| K'iche' | `myn/quc` |
| Quenya | `art/qya` |
| Romanian | `roa/ro` |
| Russian | `zle/ru` |
| Russian (Classic) | `zle/ru-cl` |
| Russian (Latvia) | `zle/ru-LV` |
| Sindhi | `inc/sd` |
| Shan (Tai Yai) | `tai/shn` |
| Sinhala | `inc/si` |
| Sindarin | `art/sjn` |
| Slovak | `zlw/sk` |
| Slovenian | `zls/sl` |
| Lule Saami | `urj/smj` |
| Albanian | `ine/sq` |
| Serbian | `zls/sr` |
| Swedish | `gmq/sv` |
| Swahili | `bnt/sw` |
| Tamil | `dra/ta` |
| Telugu | `dra/te` |
| Thai | `tai/th` |
| Tigrinya | `sem/ti` |
| Turkmen | `trk/tk` |
| Setswana | `bnt/tn` |
| Turkish | `trk/tr` |
| Tatar | `trk/tt` |
| Uyghur | `trk/ug` |
| Ukrainian | `zle/uk` |
| Urdu | `inc/ur` |
| Uzbek | `trk/uz` |
| Vietnamese (Northern) | `aav/vi` |
| Vietnamese (Central) | `aav/vi-VN-x-central` |
| Vietnamese (Southern) | `aav/vi-VN-x-south` |
| xextan-test | `art/xex` |
| Chinese (Cantonese) | `sit/yue` |
| Chinese (Cantonese, latin as Jyutping) | `sit/yue-Latn-jyutping` |

> **Note:** Constructed and conlang voices — Esperanto (`art/eo`), Lojban (`art/jbo`),
> Klingon (`art/piqd`), Quenya (`art/qya`), Sindarin (`art/sjn`), Lingua Franca Nova
> (`art/lfn`), Ido (`art/io`), Interlingua (`art/ia`), Lang Belta (`art/qdb`),
> Pyash (`art/py`), xextan-test (`art/xex`) — are grouped under the `art/`
> (artificial language) family.

## Example

```scheme
(import (curry tts))

(tts-set-rate!   150)
(tts-set-pitch!   45)
(tts-set-volume! 120)

(for-each tts-speak
  '("He hath showed thee, O man, what is good;"
    "and what doth the LORD require of thee,"
    "but to do justly, and to love mercy,"
    "and to walk humbly with thy God?"))
```

See also `examples/tts_micah.scm` for a complete runnable script.
