import csv
import struct
import re
import sys

# Increase CSV field size limit to handle large fields
csv.field_size_limit(sys.maxsize)

sys.setrecursionlimit(50000)

_WORD_PATTERN = re.compile(r"\w+", re.UNICODE)
# Sépare les suffixes ordinaux (e, er, ère, re, es) des chiffres romains/arabes
# Ex: "XXe" → "XX e", "1er" → "1 er", "XIXe" → "XIX e"
# Exige au moins 2 chiffres romains pour éviter de splitter des mots normaux (Les, De, etc.)
_ORDINAL_PATTERN = re.compile(r'\b([IVXLCDM]{2,}|\d+)(e|er|ère|re|es)\b', re.UNICODE)

def process_paragraph(text):
    text = _ORDINAL_PATTERN.sub(r'\1 \2', text)
    lengths = []
    for match in _WORD_PATTERN.finditer(text):
        lengths.append(min(len(match.group()), 255))
    return bytes(lengths)

def csv_to_bin_optimized(csv_path, bin_path):
    entries = []
    
    try:
        with open(csv_path, 'r', encoding='utf-8') as f:
            f.readline()
            
            offset = f.tell()
            line = f.readline()
            while line:
                next_offset = f.tell()
                parts = line.split(',', 1)
                
                if len(parts) >= 2:
                    content = parts[1]
                    if content.startswith('"') and content.endswith('"\n'):
                         content = content[1:-2]
                    
                    pattern = process_paragraph(content)
                    entries.append((pattern, offset))

                offset = next_offset
                line = f.readline()
                
    except FileNotFoundError:
        return

    entries.sort(key=lambda x: x[0])

    nodes_indices = []
    nodes_links = []
    
    def recursive_build(left, right):
        if left > right:
            return -1
        
        mid = (left + right) // 2
        
        current_node_idx = len(nodes_indices)
        nodes_indices.append(mid)
        nodes_links.append([-1, -1]) 
        
        l_child = recursive_build(left, mid - 1)
        r_child = recursive_build(mid + 1, right)
        
        nodes_links[current_node_idx][0] = l_child
        nodes_links[current_node_idx][1] = r_child
        
        return current_node_idx

    root_idx = recursive_build(0, len(entries) - 1)
    
    node_file_offsets = [0] * len(nodes_indices)
    current_file_pos = 8
    
    with open(csv_path, 'r', encoding='utf-8') as f:
        for i, entry_idx in enumerate(nodes_indices):
            node_file_offsets[i] = current_file_pos
            
            pattern, csv_offset = entries[entry_idx]
            
            f.seek(csv_offset)
            line = f.readline()
            
            reader = csv.reader([line])
            row = next(reader)
            title = row[0]
            
            title_bytes = title.encode('utf-8')
            
            size = 8 + 2 + len(title_bytes) + 2 + len(pattern)
            current_file_pos += size

    with open(bin_path, 'wb') as bin_file, open(csv_path, 'r', encoding='utf-8') as csv_file:
        bin_file.write(struct.pack('<I', len(nodes_indices)))
        bin_file.write(struct.pack('<I', node_file_offsets[root_idx] if root_idx != -1 else 0))
        
        for i, entry_idx in enumerate(nodes_indices):
            pattern, csv_offset = entries[entry_idx]
            
            csv_file.seek(csv_offset)
            line = csv_file.readline()
            reader = csv.reader([line])
            row = next(reader)
            title = row[0]
            
            title_bytes = title.encode('utf-8')
            
            left_node_idx = nodes_links[i][0]
            right_node_idx = nodes_links[i][1]
            
            left_off = node_file_offsets[left_node_idx] if left_node_idx != -1 else 0
            right_off = node_file_offsets[right_node_idx] if right_node_idx != -1 else 0
            
            bin_file.write(struct.pack('<II', left_off, right_off))
            bin_file.write(struct.pack('<H', len(title_bytes)))
            bin_file.write(title_bytes)
            bin_file.write(struct.pack('<H', len(pattern)))
            bin_file.write(pattern)

if __name__ == '__main__':
    csv_to_bin_optimized('wikipedia.csv', 'wikipedia.bin')