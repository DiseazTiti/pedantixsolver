import csv
import re
from libzim.reader import Archive
from bs4 import BeautifulSoup
from tqdm import tqdm

def clean_text(text):
    text = re.sub(r'\[.*?\]', '', text)
    return ' '.join(text.split())

def extract_to_csv(zim_path, output_csv, error_csv="wikipedia_error.csv"):
    archive = Archive(zim_path)
    total_entries = archive.all_entry_count
    
    print(f"Extraction de {zim_path} vers {output_csv}...")

    with open(output_csv, mode='w', encoding='utf-8', newline='') as f, \
         open(error_csv, mode='w', encoding='utf-8', newline='') as f_error:
        writer = csv.writer(f, quoting=csv.QUOTE_ALL)
        writer.writerow(['Titre', 'Premier_Paragraphe'])
        
        error_writer = csv.writer(f_error, quoting=csv.QUOTE_ALL)
        error_writer.writerow(['Titre'])

        for idx in tqdm(range(total_entries), total=total_entries, unit="pages"):
            entry = archive._get_entry_by_id(idx)
            if not entry.is_redirect:
                max_retries = 3
                success = False
                
                for attempt in range(max_retries):
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
                        
                        success = True
                        break

                    except Exception:
                        continue
                
                if not success:
                    error_writer.writerow([entry.title])

if __name__ == '__main__':
    extract_to_csv("wikipedia_fr_all_nopic_2026-01.zim", "wikipedia.csv")