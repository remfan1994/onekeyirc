Use signals varsayılan olarak no değerindedir. Ctrl-C ve Ctrl-Z, gönderilmemiş ileti veya komutu iptal eder; uygulamayı kapatmaz ya da askıya almaz. Terminal sinyallerini açmak için panel 0 veya vipirc Interface ayarlarında yes seçin. Apply hemen uygulanır; Save ayrıca [input] use_signals=true|false değerini kaydeder. Çıkışta veya askıya almada eski terminal ayarları geri yüklenir.

vipirc 0.7.37: optional language + Arabic RTL patch r28
English source: vipirc-0.7.37-english-source-r1.tar.gz
Patch archive: vipirc-0.7.37-languages-r28.tar.gz

    tar -xzf vipirc-0.7.37-english-source-r1.tar.gz
    tar -xzf vipirc-0.7.37-languages-r28.tar.gz
    sh vipirc-0.7.37-languages-r28/apply.sh vipirc-0.7.37
    cd vipirc-0.7.37 && ./configure && make
    ./vipirc --version

Expected: vipirc 0.7.37 [lang-rtl-r28]
Language: VIPIRC_LANG=tr
See doc/USE-SIGNALS-tr.txt and README.txt.
Do not apply over r27 or older application versions.
