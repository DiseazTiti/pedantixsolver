import csv
import re
from libzim.reader import Archive
from bs4 import BeautifulSoup
from tqdm import tqdm

def clean_text(text):
    text = re.sub(r'\[.*?\]', '', text)
    return ' '.join(text.split())

def extract_to_csv(zim_path, output_csv):
    archive = Archive(zim_path)
    total_entries = archive.all_entry_count
    
    print(f"Extraction de {zim_path} vers {output_csv}...")

    with open(output_csv, mode='w', encoding='utf-8', newline='') as f:
        writer = csv.writer(f, quoting=csv.QUOTE_ALL)
        writer.writerow(['Titre', 'Premier_Paragraphe'])

        for idx in tqdm(range(total_entries), total=total_entries, unit="pages"):
            entry = archive._get_entry_by_id(idx)
            if not entry.is_redirect:
                try:
                    item = entry.get_item()
                    content = item.content.tobytes().decode('utf-8')
                    soup = BeautifulSoup(content, 'html.parser')

                    first_p_text = ""
                    for p in soup.find_all('p'):
                        text = p.get_text().strip()
                        if len(text) > 60:
                            first_p_text = clean_text(text)
                            break
                    
                    if first_p_text:
                        writer.writerow([entry.title, first_p_text])

                except Exception:
                    continue

if __name__ == '__main__':
    extract_to_csv("wikipedia_fr_all_nopic_2026-01.zim", "wikipedia.csv")