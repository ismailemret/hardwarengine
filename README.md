# hardwarengine

Kernel düzeyinde (Ring 0) donanım register'larına doğrudan erişerek CPU, GPU ve sistem kaynaklarını mikrosaniye gecikmeyle izleyen, sıfır-overhead hedefli düşük seviyeli C/C++ telemetri ve HUD motoru.

WMI, COM veya hantal 3. parti katmanlar kullanılmadan; PawnIO modern kernel arayüzü, mimariye özel MSR (Model-Specific Registers), native NT API çağrıları ve dinamik donanım kütüphaneleri üzerinden veri toplanır.

---

## Mimari ve Temel Prensipler

* **Modern Kernel Köprüsü (PawnIO Entegrasyonu):** Eski, savunmasız sürücüler (WinRing0 vb.) yerine Windows HVCI (Hypervisor-Protected Code Integrity) ve Çekirdek Yalıtım (Core Isolation) ile tam uyumlu çalışan modern **PawnIO** altyapısı kullanılır. Ring 0 MSR okumaları, `PawnIOLib.dll` üzerinden doğrudan yürütülebilir bytecode blob'ları (`IntelMSR.bin`, `AMDFamily17.bin`) ile donanıma güvenli ve imza bloklarına takılmadan iletilir.
* **Zero-Abstraction Data Pipeline:** WMI/CIM katmanlarının getirdiği 100-300ms gecikme ve yüksek CPU maliyeti yerine doğrudan donanım portları, MSR register'ları ve sürücü yürütme kanalları kullanılır.
* **Hibrit Çekirdek ve Topoloji Analizi:** `GetLogicalProcessorInformationEx` ile sistem topolojisi dinamik taranır; Intel Performance (P) ve Efficient (E) çekirdekleri veya AMD Zen CCX yapıları thread bazında izole edilir.
* **Deterministic Kernel Timer:** Telemetri örnekleme döngüsü standart `Sleep()` yerine `CreateWaitableTimerExW` (High Resolution Waitable Timer) ile kernel düzeyinde 100 ms (10 Hz) periyotla çalışır; 2.0 saniyelik hareketli ortalama filtresiyle işlenir.
* **Minimal Memory & CPU Footprint:** Harici runtime bağımlılığı yoktur. Bellek kullanımı PSAPI üzerinden sürekli izlenir (hedef: < 2 MB Working Set, <%0.05 CPU yükü).

---

## Katmanlar ve Çalışma Mekanizması

### 1. CPU Telemetrisi & Ring 0 MSR Katmanı (PawnIO Pipeline)
* **Sürücü & Bytecode Motoru:** Windows Registry üzerinden tespit edilen `PawnIOLib.dll` dinamik bağlanır (`pawnio_open`, `pawnio_load`, `pawnio_execute`). İşlemci mimarisine uygun bytecode blob'u belleğe yüklenir ve thread affinity kilitlenerek `ioctl_read_msr` rutinleri çalıştırılır.
* **Intel MSR Decoding:**
  * `0x1A2` (`IA32_TEMPERATURE_TARGET`): Çip düzeyinde dinamik hardware TjMax çözünürlüğü.
  * `0x19C` (`IA32_THERM_STATUS`): Dijital termal sensör (DTS) okuması ve hardware PROCHOT (thermal throttling) tespiti.
  * `0x198` (`IA32_PERF_STATUS`): Donanımsal VID üzerinden anlık çekirdek voltajı hesabı.
  * `0x0E7` / `0x0E8` (`IA32_MPERF` / `IA32_APERF`): Donanımsal saat çevrim oranıyla gerçek efektif çekirdek frekansı hesabı.
* **AMD Zen Architecture:**
  * `0xC0010064` (`MSR_AMD_PSTATE_0`): P-State FID/DID çarpanları ve voltaj hesaplama.
  * `0xC0010293` (`MSR_AMD_HARDWARE_THERMAL`): Tctl/Tdie referans tavanı ve termal durum takibi.
* **Kullanım Yüzdesi:** `ntdll.dll` içerisinden doğrudan çözülen `NtQuerySystemInformation` (`SystemProcessorPerformanceInformation`) ile kernel/user/idle delta analizi.

### 2. GPU Telemetri Katmanı (Geliştirme Aşamasında)
* **Dinamik Runtime Binding:** Derleme anında `.lib` bağımlılığı olmadan çalışma anında (`LoadLibraryA` / `GetProcAddress`) NVML (`nvml.dll`) arayüzü bağlanır.
* **NVIDIA (NVML):** Çekirdek frekansı, bellek frekansı, sıcaklık, hotspot, fan RPM, güç tüketimi (mW) ve anlık VRAM kullanımı.
* **DirectX Fallback:** NVML bulunamayan sistemlerde `dxgi.dll` (`IDXGIFactory` / `IDXGIAdapter`) üzerinden donanım adı ve tahsis edilen VRAM tespiti.

### 3. Bellek & Süreç Öz-İzleme (Self-Profiling)
* **RAM:** `GlobalMemoryStatusEx` ile fiziksel/kullanılabilir bellek ve anlık yük yüzdesi.
* **Process Self-Metrics:** Motorun kendi getirdiği yükü doğrulamak için `GetProcessTimes` ve `K32GetProcessMemoryInfo` (Working Set & Private Commit Size) analitiği.

---

## Proje Tamamlandığında Sağlanacak Yetenekler

1. **Oyun İçi Düşük Gecikmeli HUD (In-Game Overlay):**
   * Raylib/DirectX framework'ü ile tam ekran oyunların üzerine doğrudan çizilen, sıfıra yakın giriş gecikmeli (input lag), GPU/CPU saatlerini, kare hızlarını ve termal sınırları gösteren donanım HUD'ı.
2. **Özel Fan Eğrisi & Güç Yönetimi:**
   * Gömülü Denetleyici (EC - Embedded Controller) register'larına erişim sağlanarak laptop/masaüstü sistemlerde BIOS sınırlamalarını aşan dinamik fan ve güç profili kontrolü.
3. **Kayıp/Tıkanıklık (Bottleneck) Analizörü:**
   * Efektif saat frekansları ile çekirdek yük delta analizi üzerinden termal kısma (thermal throttle), güç sınırı (power limit throttle) ve çekirdek darboğazı durumlarının gerçek zamanlı tespiti.

---

## Yapı ve Dosya Planı

```text
hardwarengine/
├── include/
│   └── telemetry.h          # Veri yapıları, MSR adresleri ve fonksiyon prototipleri
├── src/
│   ├── cpu_sensor.c         # Topoloji tarama, __cpuid vendor ayrımı, NT API ve MSR okuyucu
│   ├── gpu_sensor.c         # Dinamik NVML bağlayıcı ve DXGI fallback katmanı
│   ├── kernel_utils.c       # PawnIO library binder ve MSR bytecode yürütücü
│   ├── ram_sensor.c         # Win32 fiziksel bellek durum okuyucu
│   ├── self_sensor.c        # Süreç bellek ve işlemci öz-izleme katmanı
│   └── main.c               # 100ms High-Resolution Waitable Timer ve terminal dashboard
├── bin/
│   ├── hardwarengine.exe    # Derlenen ana ikili
│   ├── IntelMSR.bin         # PawnIO Intel MSR bytecode modülü
│   └── AMDFamily17.bin      # PawnIO AMD Zen MSR bytecode modülü
└── README.md
