import csv
import re
import logging
import multiprocessing
import os
from libzim.reader import Archive
from bs4 import BeautifulSoup
from tqdm import tqdm

logging.basicConfig(
    level=logging.WARNING,
    format="%(asctime)s [%(levelname)s] %(message)s",
)
logger = logging.getLogger(__name__)

MAX_RETRIES = 3
CHUNK_SIZE = 1000


def clean_text(text):
    text = re.sub(r"\[.*?\]", "", text)
    return " ".join(text.split())


def process_chunk(args):
    zim_path, start, end = args
    archive = Archive(zim_path)
    results = []
    errors = []

    for idx in range(start, end):
        entry = archive._get_entry_by_id(idx)
        if not entry.is_redirect:
            success = False
            last_exception = None

            for attempt in range(1, MAX_RETRIES + 1):
                try:
                    item = entry.get_item()
                    content = item.content.tobytes().decode("utf-8")
                    soup = BeautifulSoup(content, "html.parser")

                    first_p_text = ""
                    for p in soup.find_all("p"):
                        text = clean_text(p.get_text())
                        if text:
                            first_p_text = text
                            if len(text) > 60:
                                break

                    if first_p_text:
                        results.append((entry.title, first_p_text))

                    success = True
                    break

                except Exception as exc:
                    last_exception = exc
                    logger.warning(
                        "Tentative %d/%d échouée pour id=%d: %s",
                        attempt,
                        MAX_RETRIES,
                        idx,
                        exc,
                    )

            if not success:
                error_msg = str(last_exception) if last_exception else "Inconnu"
                errors.append((idx, entry.title, error_msg))
                logger.error(
                    "Échec définitif pour id=%d après %d tentatives",
                    idx,
                    MAX_RETRIES,
                )

    return results, errors, end - start


def extract_to_csv(zim_path, output_csv, error_csv="error.csv", num_workers=None):
    archive = Archive(zim_path)
    total_entries = archive.all_entry_count
    del archive

    if num_workers is None:
        num_workers = os.cpu_count() or 4

    chunks = [
        (zim_path, start, min(start + CHUNK_SIZE, total_entries))
        for start in range(0, total_entries, CHUNK_SIZE)
    ]

    print(
        f"Extraction de {zim_path} vers {output_csv} "
        f"({num_workers} workers, {len(chunks)} chunks)..."
    )

    error_count = 0

    with (
        open(output_csv, mode="w", encoding="utf-8", newline="") as f,
        open(error_csv, mode="w", encoding="utf-8", newline="") as f_error,
        multiprocessing.Pool(num_workers) as pool,
    ):
        writer = csv.writer(f, quoting=csv.QUOTE_ALL)
        writer.writerow(["Titre", "Premier_Paragraphe"])

        error_writer = csv.writer(f_error, quoting=csv.QUOTE_ALL)
        error_writer.writerow(["ID", "Titre", "Erreur"])

        with tqdm(total=total_entries, unit="pages") as pbar:
            for results, errors, chunk_size in pool.imap_unordered(
                process_chunk, chunks
            ):
                for title, text in results:
                    writer.writerow([title, text])

                for idx, title, error_msg in errors:
                    error_writer.writerow([idx, title, error_msg])
                    error_count += 1

                pbar.update(chunk_size)

    if error_count:
        print(f"\n⚠  {error_count} article(s) en erreur → {error_csv}")
    else:
        print("\n✓ Extraction terminée sans erreur.")


if __name__ == "__main__":
    extract_to_csv("wikipedia_fr_all_nopic_2026-01.zim", "wikipedia.csv")
