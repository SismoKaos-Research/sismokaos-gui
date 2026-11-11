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
| run (noun: of a recipe on a case, or of a tool on its own page) | çalıştırma | ? one word on every page since 0.4.0; the tool pages said "koşu" before ("koşu" is common in machine-learning talk, "çalıştırma" in software) |
| Run (button) / Run name | Çalıştır / Çalıştırma adı | |
| run again / Stop this run | yeniden çalıştır / bu çalıştırmayı durdur | |
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
| Data / Runs (columns of the cases table) | Veri / Çalıştırmalar | |
| duplicate (a case, a recipe) | çoğalt | |
| put back (a removed case) | geri koy | |
| More (menu of further actions) | Diğer | ? "Daha fazla" |
| leave out / replace / add again (an earthquake that is already a case) | dışarıda bırak / değiştir / yeniden ekle | |
| Activity (what ran, when) | Etkinlik | |
| took (duration of a run) / ended | sürdü / bitti | |
| leftover (files no result needs) | artık | ? "kalıntı" |
| Study on disk | Diskteki çalışma | |
| temporary space | geçici alan | |
| made by an earlier version | önceki bir sürümle yapıldı | |
| the study is held by / open in another window | başka bir pencerede açık; "elinde tutmuyor" | ? "held" has no neat word |
| Reload (the study from disk) | Yeniden yükle | |

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
| sensor (one instrument of a station: HH, HN) | algılayıcı | ? "sensör" is also used; "cihaz" is kept for the portal's instrument list |
| broadband / accelerometer | geniş bant / ivmeölçer | |
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
| request (to AFAD) / request slot | istek / istek yuvası | ? "yuva" for slot |
| still to get / asked for already | alınacak / zaten istendi | |
| Retry failed / Resume the download | Başarısızları yeniden dene / İndirmeyi sürdür | |
| All downloads (advanced) | Tüm indirmeler (gelişmiş) | |
| notification / keep awake | bildirim / uyanık tut | |
| diagnostics | tanılama bilgisi | |
| manual | kılavuz | |
| clipboard | pano | |
| Follow (the log) | İzle | |
| guide (line under the pointer on a plot) | kılavuz çizgi | |
| legend (of a plot or map) | açıklama | ? "lejant" is the map-maker's word; not "gösterge", which is an indicator |
| Stopping… / Quit now | Durduruluyor… / Şimdi çık | |
| first time here? / Got it | İlk kez mi buradasınız? / Anladım | |
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
