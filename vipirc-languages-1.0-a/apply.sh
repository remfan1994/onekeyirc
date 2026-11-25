#!/bin/sh
set -eu
# Installer messages are available before the application is built.
message() {
    case ${VIPIRC_LANG:-en}:$1 in
        id:usage) label='Penggunaan' ;;
        id:directory) label='Diperlukan direktori sumber biasa dan paket bahasa yang valid.' ;;
        id:patched) label='Sumber sudah ditambal atau dikonfigurasi. Ekstrak kembali sumber bahasa Inggris r1 ke direktori baru.' ;;
        id:source) label='Diperlukan sumber bahasa Inggris r1 untuk' ;;
        id:symlink) label='Tautan simbolis ditemukan. Pemasangan dihentikan.' ;;
        id:hash) label='Diperlukan sha256sum atau sha256.' ;;
        id:missing) label='Berkas sumber hilang atau tidak aman' ;;
        id:modified) label='Berkas sumber tidak sesuai atau telah diubah' ;;
        id:applied) label='Paket bahasa dan dukungan teks kanan-ke-kiri telah dipasang' ;;
        id:version) label='Kompilasi, lalu jalankan --version dan periksa penanda ini' ;;
        ms:usage) label='Penggunaan' ;;
        ms:directory) label='Direktori sumber biasa dan pek bahasa yang sah diperlukan.' ;;
        ms:patched) label='Sumber sudah ditampal atau dikonfigurasi. Ekstrak semula sumber bahasa Inggeris r1 ke direktori baharu.' ;;
        ms:source) label='Sumber bahasa Inggeris r1 diperlukan untuk' ;;
        ms:symlink) label='Pautan simbolik ditemui. Pemasangan dihentikan.' ;;
        ms:hash) label='sha256sum atau sha256 diperlukan.' ;;
        ms:missing) label='Fail sumber tiada atau tidak selamat' ;;
        ms:modified) label='Fail sumber tidak sepadan atau telah diubah' ;;
        ms:applied) label='Pek bahasa dan sokongan teks kanan ke kiri telah dipasang' ;;
        ms:version) label='Kompil, kemudian jalankan --version dan semak penanda ini' ;;
        es:usage) label='Uso' ;;
        es:directory) label='Se necesitan un directorio de fuentes normal y un paquete de idiomas válido.' ;;
        es:patched) label='Las fuentes ya tienen un parche o ya se han configurado. Extraiga las fuentes en inglés r1 en un directorio nuevo.' ;;
        es:source) label='Se necesitan las fuentes en inglés r1 de' ;;
        es:symlink) label='Se ha encontrado un enlace simbólico. Se ha detenido la instalación.' ;;
        es:hash) label='Se necesita sha256sum o sha256.' ;;
        es:missing) label='Falta el archivo fuente o no es seguro' ;;
        es:modified) label='El archivo fuente no coincide o se ha modificado' ;;
        es:applied) label='Se ha instalado el paquete de idiomas con soporte para escritura de derecha a izquierda' ;;
        es:version) label='Compile, ejecute --version y compruebe que aparece esta marca' ;;
        pt:usage) label='Uso' ;;
        pt:directory) label='É necessário um diretório de código-fonte comum e um pacote de idiomas válido.' ;;
        pt:patched) label='O código-fonte já foi modificado pelo instalador ou configurado. Extraia o código-fonte em inglês r1 em um novo diretório.' ;;
        pt:source) label='É necessário o código-fonte em inglês r1 de' ;;
        pt:symlink) label='Foi encontrado um link simbólico. A instalação foi interrompida.' ;;
        pt:hash) label='É necessário ter sha256sum ou sha256.' ;;
        pt:missing) label='Arquivo de origem ausente ou inseguro' ;;
        pt:modified) label='Arquivo de origem incorreto ou modificado' ;;
        pt:applied) label='Pacote de idiomas com suporte à escrita da direita para a esquerda instalado' ;;
        pt:version) label='Compile, execute --version e confira se esta marca aparece' ;;
        fr:usage) label='Utilisation' ;;
        fr:directory) label='Il faut un répertoire source ordinaire et un paquet de langues valide.' ;;
        fr:patched) label='Les sources sont déjà configurées ou le correctif a déjà été appliqué. Extrayez les sources anglaises r1 dans un nouveau répertoire.' ;;
        fr:source) label='Sources anglaises r1 requises pour' ;;
        fr:symlink) label='Un lien symbolique a été détecté. Installation interrompue.' ;;
        fr:hash) label='Il faut disposer de sha256sum ou de sha256.' ;;
        fr:missing) label='Fichier source absent ou non sûr' ;;
        fr:modified) label='Fichier source incorrect ou modifié' ;;
        fr:applied) label='Paquet de langues avec prise en charge de l’écriture de droite à gauche installé' ;;
        fr:version) label='Compilez, lancez --version et vérifiez la présence de cette mention' ;;
        de:usage) label='Aufruf' ;;
        de:directory) label='Ein reguläres Quellverzeichnis und ein gültiges Sprachpaket sind erforderlich.' ;;
        de:patched) label='Der Quellbaum wurde bereits gepatcht oder konfiguriert. Entpacken Sie die englischen Quellen r1 in ein neues Verzeichnis.' ;;
        de:source) label='Englische Quellen r1 erforderlich für' ;;
        de:symlink) label='Symbolischer Link gefunden. Installation abgebrochen.' ;;
        de:hash) label='sha256sum oder sha256 wird benötigt.' ;;
        de:missing) label='Quelldatei fehlt oder ist unsicher' ;;
        de:modified) label='Quelldatei ist falsch oder wurde geändert' ;;
        de:applied) label='Sprachpaket mit Unterstützung für Rechts-nach-links-Schrift installiert' ;;
        de:version) label='Kompilieren Sie das Programm und prüfen Sie mit --version, ob diese Kennung erscheint' ;;
        it:usage) label='Uso' ;;
        it:directory) label='Sono necessari una normale directory dei sorgenti e un pacchetto di lingue valido.' ;;
        it:patched) label='I sorgenti sono già stati configurati o modificati dalla patch. Estrarre i sorgenti inglesi r1 in una nuova directory.' ;;
        it:source) label='Sono richiesti i sorgenti inglesi r1 di' ;;
        it:symlink) label='È stato trovato un collegamento simbolico. Installazione interrotta.' ;;
        it:hash) label='È necessario sha256sum o sha256.' ;;
        it:missing) label='File sorgente mancante o non sicuro' ;;
        it:modified) label='File sorgente errato o modificato' ;;
        it:applied) label='Installato il pacchetto di lingue con supporto per la scrittura da destra a sinistra' ;;
        it:version) label='Compilare, eseguire --version e verificare che compaia questo indicatore' ;;
        tr:usage) label='Kullanım' ;;
        tr:directory) label='Normal bir kaynak dizini ve geçerli bir dil paketi gerekiyor.' ;;
        tr:patched) label='Kaynak koduna zaten yama uygulanmış veya kaynak kodu yapılandırılmış. İngilizce r1 kaynaklarını yeni bir dizine çıkarın.' ;;
        tr:source) label='İngilizce r1 kaynak kodu gereken sürüm' ;;
        tr:symlink) label='Simgesel bağ bulundu. Kurulum durduruldu.' ;;
        tr:hash) label='sha256sum veya sha256 gerekiyor.' ;;
        tr:missing) label='Kaynak dosyası eksik veya güvenli değil' ;;
        tr:modified) label='Kaynak dosyası yanlış ya da değiştirilmiş' ;;
        tr:applied) label='Sağdan sola yazı desteği içeren dil paketi kuruldu' ;;
        tr:version) label='Derledikten sonra --version seçeneğiyle şu işareti kontrol edin' ;;
        ru:usage) label='Использование' ;;
        ru:directory) label='Требуются обычный каталог исходного кода и исправный языковой пакет.' ;;
        ru:patched) label='Исходный код уже настроен или к нему применён патч. Распакуйте английские исходники r1 в новый каталог.' ;;
        ru:source) label='Требуются английские исходники r1 для' ;;
        ru:symlink) label='Обнаружена символическая ссылка. Установка остановлена.' ;;
        ru:hash) label='Требуется sha256sum или sha256.' ;;
        ru:missing) label='Исходный файл отсутствует или небезопасен' ;;
        ru:modified) label='Исходный файл не соответствует ожидаемому или был изменён' ;;
        ru:applied) label='Установлен языковой пакет с поддержкой письма справа налево' ;;
        ru:version) label='Соберите программу, запустите её с параметром --version и проверьте наличие метки' ;;
        ar:usage) label='طريقة الاستخدام' ;;
        ar:directory) label='يلزم مجلد عادي لملفات المصدر وحزمة لغات صالحة.' ;;
        ar:patched) label='سبق تطبيق الرقعة أو تهيئة ملفات المصدر. فك ضغط المصدر الإنجليزي r1 في مجلد جديد.' ;;
        ar:source) label='يلزم المصدر الإنجليزي r1 للبرنامج' ;;
        ar:symlink) label='عُثر على رابط رمزي. أُوقف التثبيت.' ;;
        ar:hash) label='يلزم توفر sha256sum أو sha256.' ;;
        ar:missing) label='ملف المصدر مفقود أو غير آمن' ;;
        ar:modified) label='ملف المصدر غير مطابق أو جرى تعديله' ;;
        ar:applied) label='ثُبّتت حزمة اللغات مع دعم الكتابة من اليمين إلى اليسار' ;;
        ar:version) label='بعد بناء البرنامج، شغّله مع الخيار --version وتحقق من ظهور العلامة' ;;
        id:integrity) label='Pemeriksaan integritas paket bahasa gagal' ;;
        id:unsafe) label='Berkas tidak aman atau tujuan bertentangan' ;;
        id:applyfailed) label='Pemasangan gagal. Gunakan kembali sumber Inggris yang baru diekstrak.' ;;
        ms:integrity) label='Semakan integriti pek bahasa gagal' ;;
        ms:unsafe) label='Fail tidak selamat atau destinasi bercanggah' ;;
        ms:applyfailed) label='Pemasangan gagal. Gunakan semula sumber Inggeris yang baru diekstrak.' ;;
        tr:integrity) label='Dil paketi bütünlük denetimi başarısız' ;;
        tr:unsafe) label='Güvenli olmayan dosya veya hedef çakışması' ;;
        tr:applyfailed) label='Kurulum başarısız. İngilizce kaynakları yeniden çıkarın.' ;;
        es:integrity) label='Falló la comprobación de integridad del paquete de idiomas' ;;
        es:unsafe) label='Archivo inseguro o destino en conflicto' ;;
        es:applyfailed) label='Falló la instalación. Vuelva a extraer las fuentes en inglés.' ;;
        pt:integrity) label='Falha na verificação da integridade do pacote de idiomas' ;;
        pt:unsafe) label='Arquivo inseguro ou destino em conflito' ;;
        pt:applyfailed) label='Falha na instalação. Extraia novamente o código-fonte em inglês.' ;;
        fr:integrity) label='Échec du contrôle d’intégrité du paquet de langues' ;;
        fr:unsafe) label='Fichier non sûr ou conflit de destination' ;;
        fr:applyfailed) label='Échec de l’installation. Extrayez de nouveau les sources anglaises.' ;;
        de:integrity) label='Integritätsprüfung des Sprachpakets fehlgeschlagen' ;;
        de:unsafe) label='Unsichere Datei oder Zielkonflikt' ;;
        de:applyfailed) label='Installation fehlgeschlagen. Entpacken Sie die englischen Quellen erneut.' ;;
        ru:integrity) label='Проверка целостности языкового пакета не пройдена' ;;
        ru:unsafe) label='Небезопасный файл или конфликт назначения' ;;
        ru:applyfailed) label='Установка не удалась. Распакуйте английские исходники заново.' ;;
        it:integrity) label='Verifica di integrità del pacchetto di lingue non riuscita' ;;
        it:unsafe) label='File non sicuro o conflitto di destinazione' ;;
        it:applyfailed) label='Installazione non riuscita. Estrai nuovamente i sorgenti inglesi.' ;;
        ar:integrity) label='فشل التحقق من سلامة حزمة اللغات' ;;
        ar:unsafe) label='ملف غير آمن أو تعارض في الوجهة' ;;
        ar:applyfailed) label='فشل التثبيت. فك ضغط المصدر الإنجليزي من جديد.' ;;
        *) printf '%s\n' "$2"; return ;;
    esac
    if [ "$#" -eq 3 ]; then
        printf '%s: %s\n' "$label" "$3"
    else
        printf '%s\n' "$label"
    fi
}

if [ "$#" -ne 1 ]; then
    message usage "usage: sh apply.sh SOURCE_TREE" "sh apply.sh SOURCE_TREE" >&2
    exit 2
fi
target=$1
dir=$(CDPATH= cd "$(dirname "$0")" && pwd)
if [ ! -d "$target" ] || [ -L "$target" ] || [ ! -d "$dir/overlay" ]; then
    message directory "A regular source directory and a valid overlay are required" >&2
    exit 1
fi
if [ -e "$target/LANGUAGE-PACK-REVISION.txt" ] || [ -e "$target/Makefile" ]; then
    message patched "Already patched or configured: use fresh English source r1" >&2
    exit 1
fi
if [ ! -f "$target/SOURCE-REVISION.txt" ] ||
   ! grep -Fxq 'APPLICATION VERSION: 1.0-a' "$target/SOURCE-REVISION.txt" ||
   ! grep -Fxq 'ENGLISH SOURCE REVISION: r1' "$target/SOURCE-REVISION.txt"; then
    message source "The source revision is not vipirc 1.0-a English r1" "vipirc 1.0-a" >&2
    exit 1
fi
# Refuse special files and hard links as well as symlinks. A fresh tree and
# a privately owned, unmodified extraction directory are required. This is
# an integrity check, not an authenticity signature or a concurrent-writer lock.
unsafe_tree() {
    invalid=$(cd "$1" && find . ! -type f ! -type d -print) || return 0
    [ -z "$invalid" ] || return 0
    invalid=$(cd "$1" && find . -type f -links +1 -print) || return 0
    [ -z "$invalid" ] || return 0
    return 1
}
if unsafe_tree "$target" || unsafe_tree "$dir"; then
    message unsafe "Unsafe file or destination conflict" >&2
    exit 1
fi
if command -v sha256sum >/dev/null 2>&1; then
    file_sha256() { sha256sum "$1" | awk '{print $1}'; }
elif command -v sha256 >/dev/null 2>&1; then
    file_sha256() { sha256 -q "$1"; }
else
    message hash "SHA-256 utility required (sha256sum or sha256)" >&2
    exit 1
fi
# Check the inventory and hashes before modifying any source files. The
# published external archive hash is still needed to establish authenticity.
scratch=$(mktemp -d "${TMPDIR:-/tmp}/irc-language-pack.XXXXXXXX") || {
    message applyfailed "Installation failed. Re-extract the English source." >&2
    exit 1
}
scratch=$(CDPATH= cd "$scratch" && pwd)
trap 'rm -rf "$scratch"' 0
trap 'exit 1' HUP INT TERM
integrity_error() {
    message integrity "Language pack integrity check failed" "$1" >&2
    exit 1
}
unsafe_error() {
    message unsafe "Unsafe file or destination conflict" "$1" >&2
    exit 1
}
safe_relative() {
    case $1 in ''|/*|*[!A-Za-z0-9._/-]*) return 1 ;; esac
    case /$1/ in */../*|*/./*|*//* ) return 1 ;; esac
    return 0
}
safe_digest() {
    case $1 in *[!0123456789abcdef]*|'') return 1 ;; esac
    [ "${#1}" -eq 64 ]
}
[ -f "$dir/PACK-SHA256SUMS" ] || integrity_error PACK-SHA256SUMS
: > "$scratch/listed"
while read -r expected rel; do
    safe_digest "$expected" && safe_relative "$rel" || integrity_error PACK-SHA256SUMS
    [ "$rel" != PACK-SHA256SUMS ] && [ -f "$dir/$rel" ] || integrity_error "$rel"
    actual=$(file_sha256 "$dir/$rel") || integrity_error "$rel"
    [ "$actual" = "$expected" ] || integrity_error "$rel"
    printf '%s\n' "$rel" >> "$scratch/listed"
done < "$dir/PACK-SHA256SUMS"
(cd "$dir" && find . -type f -print) > "$scratch/raw" || integrity_error inventory
sed -e 's|^\./||' -e '/^PACK-SHA256SUMS$/d' "$scratch/raw" > "$scratch/actual"
LC_ALL=C sort "$scratch/actual" > "$scratch/sorted-actual"
LC_ALL=C sort "$scratch/listed" > "$scratch/sorted-listed"
cmp -s "$scratch/sorted-actual" "$scratch/sorted-listed" || integrity_error inventory
# Validate every source file to be replaced and remember these paths. Reject
# collisions with new overlay files (including directory/file mismatches).
: > "$scratch/replaced"
while read -r expected rel; do
    safe_digest "$expected" && safe_relative "$rel" || integrity_error BASE-SHA256SUMS
    [ -f "$dir/overlay/$rel" ] || integrity_error "$rel"
    if [ ! -f "$target/$rel" ]; then
        message missing "Missing or unsafe source: $rel" "$rel" >&2
        exit 1
    fi
    actual=$(file_sha256 "$target/$rel") || integrity_error "$rel"
    if [ "$actual" != "$expected" ]; then
        message modified "Wrong or modified source: $rel" "$rel" >&2
        exit 1
    fi
    printf '%s\n' "$rel" >> "$scratch/replaced"
done < "$dir/BASE-SHA256SUMS"
(cd "$dir/overlay" && find . -type f -print) > "$scratch/files" || integrity_error overlay
while IFS= read -r path; do
    rel=${path#./}
    safe_relative "$rel" || unsafe_error "$rel"
    if [ -e "$target/$rel" ] && ! grep -Fxq "$rel" "$scratch/replaced"; then
        unsafe_error "$rel"
    fi
done < "$scratch/files"
(cd "$dir/overlay" && find . -type d -print) > "$scratch/dirs" || integrity_error overlay
while IFS= read -r path; do
    [ "$path" = . ] && continue
    rel=${path#./}
    safe_relative "$rel" || unsafe_error "$rel"
    if [ -e "$target/$rel" ] && [ ! -d "$target/$rel" ]; then
        unsafe_error "$rel"
    fi
done < "$scratch/dirs"
# Stage tar creation first so a failure on the producing side cannot be
# masked by the last process in a POSIX shell pipeline. Extraction errors
# may still leave a partial tree; never report success in that case.
if ! (cd "$dir/overlay" && tar -cf "$scratch/overlay.tar" .) ||
   ! (cd "$target" && tar -xf "$scratch/overlay.tar"); then
    message applyfailed "Installation failed. Re-extract the English source." >&2
    exit 1
fi
message applied "Optional language+RTL patch 1.0-a applied to vipirc 1.0-a." "vipirc 1.0-a (1.0-a)"
message version "Compile, then use --version to check [lang-rtl-1.0-a]." "[lang-rtl-1.0-a]"
