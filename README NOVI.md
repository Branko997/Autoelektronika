# Autoelektronika

Za otvaranje periferija se koriste batch skripte koje se nalaze u folderima od periferija i imaju iste nazive kao .exe fajlovi. 

Za led bar dobiju se cetri stuba prvi (plavi) je ulazni i kod njega donje tri diode su odabir moda, prva odgore je MONITOR, druga DRIVE, treca SPEED. Drugi stub pokazuje da sistem radi tj. donja dioda sija ako je upaljen neki od tri moda, a ako ne sija nijedan nije odabran. Izlazne diode u drugom stubu svetle periodom od 200ms ako je upaljen alarm, a on se pali ako je rashladna tecnost toplija od 110 stepeni ili ako su obrtaji preko 6000.Treci i cetvrti stub prikazuju vrednosti dva senzora u odgovarajucem modu u realizaciji kao led vu metar. Ako je vise dugmadi pritisnuto odjednom opet ce se ugasiti sistem dok ne bude samo jedno dugme ukljuceno, jos ako je sve iskljuceno alarm se ne vidi. Zadnja komanda koja je data sistemu svejedno da li sa led bara ili serijske je ona koja je aktivna.

Za 7 segmentni displej se dobija jedan displej sa jednom cifarom i ona govori koji je mod aktivan mod: 1-MONITOR, 2-DRIVE, 3-SPEED. Kada sistem ne radi(nije odabran nijedan mod) nula je prikazana na displeju.

UniCom za senzore (Unit 0) se podesava sa AUTO odgovorom. Triger je XYZ, a senzorske informacije se salju kao \00\s1\s2\s3\s4\s5\s6\ff. S2 je temperatura rashladne tecnosti a s3,s4 su obrtaji, ovi su za testiranje alarma bitni. Ako postoji greska u slanju gasi se sistem.

UniCom za PC (Unit 1) kada se salju komande sa njega menja prikaz na 7seg displeju zavisno od trazenog moda, MONITOR\0d DRIVE\0d SPEED\0d su ako je komanda predugacka sistem ostavlja staru vrednost na displeju, a ako je pogresno uneseno gasi sistem.

Kratak opis taskova:

void SensorDataHandler(void* pvParameters)

U ovom tasku se ispituju vrednosti sa senzora, da li su unutar svog opsega i na osnovu toga se određuje da li će se u drugom stubcu led bara uključiti alarm ili samo jedna dioda koja govori da si vrednosti u opsegu. 

void led_bar_tsk(void* pvParameters)

Ovaj task služi za promenu moda samog sistema i ukoliko je došlo do promene šalje odgovarajuću poruku. Za to je iskorišćen prvi stubac led bara koji je definaisan kao ulazni. Prilikom promene potrebno je sačekati prekid koji dozvoljava semafor za promenu leda.

void Seg7Task(void* pvParameters)

Ovaj task služi za ispisivanje vrednosti na 7-seg displeju, konkretno ispisuje koji je mod aktivan u tom trenutku.0 nije ni jedan mod aktivan ili više njih aktivno u istom trenutku, 1 aktivan MONITOR mod, 2 aktivan DRIVE mod, 3 aktivan SPEED mod.

void AlarmTask(void* pvParameters)

Ovaj task implementira alarm koji je realizovan u vidu blinkanja led dioda u celog drugog stubca led bara periodom 200ms. Alaram se uključuje ako su neki senzori prešli granicu normalne vrednosti.

void Led_Manager_task(void* pvParametars) 

Ovaj task služi za "ispisivanje" vrednosti odgovarajucih senzora za određeni mod. Za svaki mod ispisuju se vrednosti dva senzora i za to su iskorišteni treći i četvrti stubac led bara u formi Vu metra.

void PC_command(void* pvParameters)

Ovaj task služi za slanje komandi(promenu moda) preko PC računara, i ako je poslata komandna reč tačna menja se mod.

void PC_SerialReceive_Task(void* pvParameters) 

Ovaj task služi za primenje pc komandi i slanje na obradu, prihvata vrednosti sa kanala 0 serijske komunikacije. Semafor zaustvlja task sve dok ne stigne karakter i ako je stigao karakter šalje se komanda i restartuje pozicija za novu komandu.

void SerialSend_Task(void* pvParameters)

Ovaj task omogućava rućno slanje podataka i automatsko slanje podataka na svakih 100ms.

void SerialReceive_Task(void* pvParameters)

Ovaj task "upravlja" prijemom podataka(vrednosti senzora) i smešta ih u red.ss Semafor zaustavlja zadatak sve dok ne stigne podatak.

void FormAndSend7SegData(sensor_val SensTemp, uint8_t Msg)

Ovaj task služi za ispitivanje trenutnog moda i u zavisnosti od toga koji je mod trenutno aktivan formatira odgovarajuće vrednosti za 7-segmentni displej.

void LedManager(sensor_val SensTemp, uint8_t Msg)

ovaj task služi ispitivanje vrednosti sa senxora i u zavisnosti od njihovih vrednosti odredjuje se koliko će led dioda upaliti(Vu metar, vrdnost senzora 0 ne sija ni jedna maksimalna vrednost senzora sve diode uključene). Vrednosti senzora svih senzora osim senzor obrtaja mogu imati vrednost od 0 do 255 i ta vrednost se deli sa 32 i tako se dobija koliko će dioda biti upaljeno, vrednost senzor obrtaja može imati vrednost od 0 do 6400 pa se ta vrednost deli sa 800 da bi smo odredili koliko dioda treba da bude upaljeno.