# Turkish glossary

The terms used in `sismokaos-gui_tr.ts`, so the wording can be reviewed in one
place. A `?` marks a term a Turkish seismologist might word differently; change
it here first, then in the `.ts` file (`update_translations` keeps finished
translations).

## The study workflow

| English | Turkish | |
|---|---|---|
| study | çalışma | |
| case | vaka | ? "olgu" or "örnek olay" are alternatives |
| recipe | tarif | ? "ayar seti" is an alternative |
| span (look-back of a case) | süre | ? "geriye bakış süresi" is more exact but long |
| run (of a recipe on a case) | çalıştırma | ? the tool pages say "koşu" for their own runs |
| run (button on a tool page, run name) | koşu / Koşu adı | ? see above; two words for one English word |
| Runs (page) | Çalıştırmalar | |
| Compare (page) | Karşılaştır | |
| done / missing / stale / waiting / failed | tamam / eksik / eskimiş / bekliyor / başarısız | ? "eskimiş" for stale |
| makes N runs stale | N çalıştırmayı eskitir | ? |
| revision (of a recipe) | sürüm | |
| study inputs | çalışma girdileri | |
| set up the study | çalışmayı kur | |
| built-in list | yerleşik liste | |
| as shipped | uygulamayla gelen | |
| Locate… / Skip / Dismiss | Bul… / Atla / Yoksay | |
| Next: … → | Sonraki: … → | |
| fitted per case | vakaya göre uydurulan | ? |
| score | skor | ? "puan", "başarım" are alternatives |
| baseline (model) | temel (model) | ? "karşılaştırma tabanı", "referans model" |
| series | seri | |
| reference date | referans tarih | |

## Seismology

| English | Turkish | |
|---|---|---|
| earthquake catalog | deprem kataloğu | |
| event | olay | |
| epicentre | dış merkez | ? "merkez üssü" is the everyday word; the file used "dış merkez" before this pass and keeps it |
| magnitude | büyüklük | |
| completeness magnitude (Mc) | tamamlılık büyüklüğü (Mc) | "Tamamlılık Mc" as a short field label |
| estimate (Mc) | kestir | |
| b-value | b-değeri | |
| depth | derinlik | |
| origin time | oluş zamanı | |
| azimuth | azimut | |
| catalog zone | katalog bölgesi | |
| fault zone | fay zonu | |
| buffer (around fault traces) | tampon | |
| fault source (hazard model) | fay kaynağı | |
| seismogenic fault sources | sismojenik fay kaynakları | ? |
| hazard model | tehlike modeli | |
| active fault map | diri fay haritası | |
| fault trace | fay izi | |
| station | istasyon | |
| network | ağ | |
| channel | kanal | |
| instruments | cihazlar | |
| sample rate | örnekleme hızı | ("Örnekleme" as a column heading) |
| waveform | dalga formu | |
| waveform folder | dalga formu klasörü | |
| record header | kayıt başlığı | |
| coverage (days of data, area covered) | kapsam | one word for both senses |
| covered / not covered (earthquake) | kapsanan / kapsanmayan | |
| quiet period (calibration) | sakin dönem | |
| calibration | kalibrasyon | |
| recalibrate | yeniden kalibre et | |
| kept / reused (calibration) | saklanan / yeniden kullanıldı | |
| queued (a cell waiting for a free slot) | sırada | the user's word |
| take out of the queue | sıradan çıkar | |
| sparse (catalog) | seyrek | ? |
| little of the span (station coverage) | sürenin azı | ? short for a table cell |
| time offset (catalog vs UTC) | zaman farkı | |
| Turkey time | Türkiye saati | |
| early warning | erken uyarı | |
| missed / detected (event) | kaçırıldı / yakalandı | ? "saptandı" for detected |

## Analysis and learning

| English | Turkish | |
|---|---|---|
| features | öznitelikler | |
| feature table | öznitelik tablosu | |
| indicators | göstergeler | |
| classification | sınıflandırma | |
| class | sınıf | |
| window / step | pencere / adım | |
| risk label | risk etiketi | |
| train (a model) | eğit | |
| validation / test days | doğrulama / test günleri | |
| embedding delay / dimension | gömme gecikmesi / gömme boyutu | ? |
| sample entropy | örneklem entropisi | ? |
| declustering | artçıları ayıklama | ? |
| configuration | yapılandırma | |
| default | varsayılan | |

## Interface

| English | Turkish | |
|---|---|---|
| folder | klasör | |
| scan / scan again | tara / yeniden tara | |
| exact scan | kesin tarama | the pass that reads every record header |
| estimate (of a file's coverage, from its name) | tahmin; "yaklaşık" before a number | ? not "kestir", which is kept for estimating Mc |
| scan cache | tarama önbelleği | |
| files read at once | aynı anda okunan dosya | |
| skipped files | atlanan dosyalar | |
| Moved… | Taşındı… | |
| export (noun, AFAD export) | dışa aktarım | |
| download / fetch | indir | "Fetch" and "Download" are both "İndir" |
| job / Jobs panel | iş / İşler paneli | |
| log / Show log | günlük / Günlüğü göster | |
| Stop | Durdur | |
| campaign | kampanya | |
| mailbox | posta kutusu | |
| ingest (an archive) | içe al | |
| plan (a station) | planla | |
| filter | süzgeç | ? "filtre" is more common in software |
| tick / ticked | işaretle / işaretli | |
| marker (on a map) | işaret | |
| candidate (station) | aday | |
| WHERE (map panel) | NEREDE | |
| preview | önizleme | |
| theme | tema | |
| public domain | kamu malı | |
| terms of use | kullanım koşulları | |

Left in English on purpose: file and folder names (`study.toml`, `run.log`,
`exports`), formats (miniSEED, StationXML, FDSN, CSV), `shapefile`, service and
dataset names (USGS, AFAD, EFSM20, MTA), and model names.
