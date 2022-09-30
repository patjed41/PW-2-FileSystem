# File system

My solution for the second project of the [Concurrent programming (pol. Programowanie współbieżne)](https://usosweb.mimuw.edu.pl/kontroler.php?_action=katalog2/przedmioty/pokazPrzedmiot&prz_kod=1000-213bPW) course taken in the 2021/2022 winter semester.

## Task

Implement concurrent data structure representing folder tree.

## Usage

To build run
```
mkdir build && cd build && cmake ../src && make
```
in the repository's main directory.

### Example

After building you can run
```
./main
```
in the `build` directory. File [main.c](https://github.com/patjed41/PW-2-FileSystem/blob/master/src/main.c) contains simple sequential test demonstrating usage of the folder tree.

# Full description in polish

Zadanie polega na zaimplementowaniu części systemu plików, a konkretnie współbieżnej struktury danych reprezentującej drzewo folderów.

Ścieżki są reprezentowane napisami postaci `"/foo/bar/baz/"`.

Należy zdefiniować strukturę `Tree` oraz następujące operacje (które będą wykonywane przez wielowątkowy program testujący):

- `Tree* tree_new()`

  Tworzy nowe drzewo folderów z jednym, pustym folderem `"/"`.

- `void tree_free(Tree*)`

  Zwalnia całą pamięć związaną z podanym drzewem.

- `char* tree_list(Tree* tree, const char* path)`

  Wymienia zawartość danego folderu, zwracając nowy napis postaci "foo,bar,baz" (wszystkie nazwy podfolderów; tylko bezpośrednich podfolderów, czyli bez wchodzenia wgłąb; w dowolnej kolejności, oddzielone przecinkami, zakończone znakiem zerowym). (Zwolnienie pamięci napisu jest odpowiedzialnością wołającego `tree_list`).

- `int tree_create(Tree* tree, const char* path)`

  Tworzy nowy podfolder (np. dla `path="/foo/bar/baz/"`, tworzy pusty podfolder `baz` w folderze `"/foo/bar/"`).

- `int tree_remove(Tree* tree, const char* path)`

  Usuwa folder, o ile jest pusty.

- `int tree_move(Tree* tree, const char* source, const char* target)`

  Przenosi folder `source` wraz z zawartością na miejsce `target` (przenoszone jest całe poddrzewo), o ile to możliwe (patrz kody błędów niżej).
  
  Przykład: jeśli istnieją ścieżki `/a/b/c/`, `/a/b/d/`, `/b/a/d/` (oraz ich przodkowie), to po operacji `tree_move(tree, "/a/b/", "/b/x/")` powinny istnieć ścieżki `/b/x/c/`, `/b/x/d/`, `/b/a/d/`, `/a/` (oraz ich przodkowie).

## Współbieżność

- Operacje `list`, `create`, `remove` i `move` muszą być atomowe (tzn. zwracane wartości powinny być takie jak gdyby operacje wykonały się sekwencyjnie w jakiejś kolejności).  
- Jeśli dwie operacje zostały wywołane równolegle (tj. nie jedna rozpoczęta po zakończeniu drugiej), to ich zwracane wartości mogą być takie jak gdyby wykonały się w dowolnej kolejności: jest OK jeśli wybrana kolejność prowadzi do zwrócenia kodu błędu.
- Można zakładać, że operacja `tree_free` zostanie wykonana na danym drzewie dokładnie raz, po zakończeniu wszystkich innych operacji.

### HashMap

- Zawartość każdego folderu powinna być utrzymywana za pomocą struktury `HashMap`, opisanej w [HashMap.h](https://github.com/patjed41/PW-2-FileSystem/blob/master/src/HashMap.h). Na przykład `hmap_new()` (tworzenie nowej mapy) powinno być wołane dokładnie raz w `tree_new()` i w `tree_create()` (o ile jest to możliwe, patrz kody błędów niżej) i w żadnej innej operacji.
- Struktura ta jest zaimplementowana naiwnie, należy samemu zadbać o synchronizację. Implementacja [HashMap.c](https://github.com/patjed41/PW-2-FileSystem/blob/master/src/HashMap.c) może zostać podmieniona. Jedyne co można o niej zakładać, to że wykonuje się poprawnie (każda operacja w skończonym czasie zakończy się oczekiwanym skutkiem), o ile żadna operacja modyfikująca (`hmap_insert/hmap_remove/hmap_free`) nie wykonuje się współbieżnie z jakąkolwiek inną operacją na tej samej mapie. Współbieżne użycie `hmap_get`, `hmap_size` i iteracji (na różnych iteratorach) jest dozwolone.

Operacje na drzewie, które mogą się wykonać równolegle przy powyższych założeniach, powinny mieć taką możliwość. W szczególności nie powinny bez uzasadnienia czekać, aż inna operacja zakończy wywołanie operacji np. `hmap_get`. Przykładowo: powinno być możliwe współbieżne wykonanie operacji `tree_create(tree, "/a/b/")` oraz `tree_create(tree, "/c/d/")`, które współbieżnie wykonują `hmap_get` na hash-mapie utrzymującej zawartość folderu `"/"`, a później współbieżnie wykonują `hmap_insert` na hash-mapach utrzymujących odpowiednio zawartość folderów `"/a/"` oraz `"/c/"`.

Należy przy tym unikać przede wszystkim zakleszczenia, ale również zagłodzenia. W kwestii zagłodzeń można założyć, że wątków jest skończenie wiele (nie tworzą się w kółko nowe, nie ma ich niepraktycznie wiele).

## Kody błędów

- Nazwy folderów są dowolnymi ciągami małych liter ASCII od `a` do `z` długości od 1 do 255.
- Ścieżki to nazwy folderów oddzielone znakiem `'/'`; zawsze zaczynają się i kończą się znakiem `'/'`; mają długość od 1 do 4095 znaków.
- `tree_list` zwraca `NULL` jeśli ścieżka nie jest prawidłowej postaci (w powyższym sensie) lub nie istnieje.
- Pozostałe operacje, jeśli ścieżka nie jest prawidłowej postaci, zwracają kod błędu `EINVAL` (stała zdefiniowana w `errno.h`).
- W `tree_create`: jeśli folder już istnieje, zwraca kod błędu `EEXIST`. Jeśli rodzic podanej ścieżki nie istnieje, zwraca `ENOENT`.
- W `tree_remove`: jeśli folder jest niepusty, zwraca `ENOTEMPTY`. Jeśli nie istnieje, zwaraca `ENOENT`.  Jeśli podano korzeń `"/"`, zwraca `EBUSY`.
- W `tree_move`: jeśli folder `source` lub rodzic folderu `target` nie istnieje, zwraca `ENOENT`. Jeśli folder `target` już istnieje, zwraca kod błedu `EEXIST`. Jeśli jako `source` podano korzeń `"/"`, zwraca `EBUSY`.
- Jeśli użyte funkcje `pthreads_*` zwrócą kod błędu, można go po prostu zwrócić.
- W przypadku kiedy operacji nie da się sensownie wykonać z jeszcze innego powodu, należy zwrócić inny kod błędu, między -1 a -20, oraz opisać w komentarzu (przy definicji odpowiedniej stałej, przy kodzie funkcji lub przy *return*) co dany bład oznacza.
- Jeśli błędów jest kilka, można zwrócić dowolny.
- Zwracając kod błędu, operacja musi pozostawić drzewo w stanie poprawnym, niezmienionym.

## Wymagania techniczne

- Nie wolno korzystać z bibliotek innych niż standardowa i systemowe. Z biblioteki `pthreads` wolno korzystać wyłącznie z typów `pthread_mutex_t`,  `pthread_cond_t` (oraz funkcji na nich). Nie wolno korzystać z `semaphore.h` (`sem_t`, `sem_open`, ...).
- Rozwiązanie powinno zawierać krótki komentarz/komentarze (po polsku lub angielsku) uzasadniające użyte metody synchronizacji. Należy przy tym założyć, że czytelnikowi znana jest treść zadania oraz problemy i konstrukcje z wykładu, ćwiczeń i laboratoriów (wraz ze swoimi własnościami, uzasadnieniami, itd.).
- Rozwiązanie powinno poprawnie zwalniać wszystkie zaalokowane zasoby (poza napisami zwróconymi przez `tree_list`), bez niepotrzebnego odkładania tego w czasie.
- Złożoność czasowa i pamięciowa nie może być niepotrzebnie duża.
- Implementacja nie tworzy własnych wątków.
- Użycie `clang-format` i `clang-tidy` nie jest wymagane (ale jest jak zwykle zalecane).
- Załączony jest szablon rozwiązania `ab1234567.tar`.
- W podanym szablonie można zmieniać jedynie plik `Tree.c`, `path_utils.*`, `err.*`, tworzyć nowe pliki `.h` i `.c` oraz dodawać ich nazwy do linijki `add_library(Tree Tree.c)` w [CMakeLists.txt](https://github.com/patjed41/PW-2-FileSystem/blob/master/src/CMakeLists.txt) (zmiany w pozostałych plikach będą ignorowane – można dla własnej wygody zmieniać np. `main.c`)
- Rozwiązanie należy wysłać na Moodle w postaci pliku `ab1234567.tar` (lub `.tar.gz`), gdzie `ab1234567` w nazwie pliku i zawartego folderu jest zmienione na własne inicjały i numer indeksu.
- Rozwiązanie musi działać na `students` skompilowane za pomocą `cmake` (polecenie `cd ab1234567/build && cmake .. && make` powinno wyprodukować plik `ab1234567/build/libTree.a`).
