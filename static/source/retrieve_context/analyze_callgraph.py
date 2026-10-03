import os
import json
import re
from collections import defaultdict

def convert_graphviz_json(input_path, output_path):
    # Load the original Graphviz JSON
    with open(input_path, 'r') as f:
        data = json.load(f)

    # Extract nodes and edges
    nodes = {}
    edges = []

    # Process nodes
    for obj in data.get('objects', []):
        if 'name' in obj and '_gvid' in obj:
            nodes[obj['_gvid']] = {'name': obj['name']}

    # Process edges
    for edge in data.get('edges', []):
        if 'tail' in edge and 'head' in edge:
            edges.append({
                'source': edge['tail'],
                'target': edge['head']
            })

    # Create new graph structure
    graph = {
        'graph': {
            'directed': True,
            'nodes': nodes,
            'edges': edges
        }
    }

    # Save the simplified JSON
    with open(output_path, 'w') as f:
        json.dump(graph, f, indent=2)

    print(f"Converted graph saved to {output_path}")
    print(f"Nodes: {len(nodes)}, Edges: {len(edges)}")

class PythonCallGraphAnalyzer:
    def __init__(self, call_graph_json):
        self.graph = self._build_graph(call_graph_json)
    
    def _build_graph(self, call_graph_json):
        """
        Builds a graph from the JSON call graph data.
        """
        graph = defaultdict(list)
        edges = call_graph_json["graph"]["edges"]
        
        for edge in edges:
            source = edge["source"]
            target = edge["target"]
            graph[source].append(target)
        
        return graph
    
    def find_all_paths(self, src, dest):
        """
        Finds all paths from `src` to `dest` in the graph.
        """
        visited = set()
        paths = []
        self._dfs(src, dest, visited, [], paths)
        return paths
    
    def _dfs(self, current, dest, visited, path, paths):
        """
        Performs a Depth-First Search to find all paths from `current` to `dest`.
        """
        visited.add(current)
        path.append(current)
        
        if current == dest:
            # If we reach the destination, add the current path to the list of paths
            paths.append(path.copy())
        else:
            # Recursively explore all neighbors
            for neighbor in self.graph[current]:
                # print(f"Current: {current}, Neighbor: {neighbor}")
                if neighbor not in visited:
                    self._dfs(neighbor, dest, visited, path, paths)
        
        # Backtrack: remove the current node from the path and mark it as unvisited
        path.pop()
        visited.remove(current)
    
    def print_graph(self):
        """
        Prints the call graph.
        """
        for source, targets in self.graph.items():
            print(f"{source}: {targets}")


def parse_call_graph_cpp(file_path):
    """
    Parse the call graph text file and build a graph representation.
    """
    graph = defaultdict(list)
    current_function = None

    with open(file_path, 'r', encoding='utf-8', errors='ignore') as file:
        for line in file:
            line = line.strip()
            if line.startswith("Call graph node for function:"):
                # Extract the current function name
                current_function = line.split("'")[1]
                # print(f"Current function: {current_function}")
            elif line.startswith("CS<"):
                if "'" not in line:
                    continue
                # Extract the called function name
                # print(f"\tLine: {line}")
                called_function = line.split("'")[1]
                # print(f"\tCalled function: {called_function}")
                graph[current_function].append(called_function)
    return graph

def find_all_paths(graph, start, end, path=None):
    """
    Find all paths from the start function to the end function using backtracking.
    """
    if path is None:
        path = []
    path = path + [start]

    if start == end:
        return [path]

    if start not in graph:
        return []

    paths = []
    for function in graph[start]:
        if function not in path:  # Avoid cycles
            new_paths = find_all_paths(graph, function, end, path)
            for new_path in new_paths:
                paths.append(new_path)

    return paths

def find_function_definitions(file_path):
    """
    Find function definitions in a source file (C/C++ or Python) using regex.
    """
    function_definitions = []

    # Determine the file type based on the extension
    if file_path.endswith(('.c', '.cpp', '.cc', '.cxx', '.h', '.hpp')):
        # Regex for C/C++ function definitions
        function_regex = re.compile(
            r'^\s*(?:\w+\s+)+(\w+)\s*\([^)]*\)\s*\{',  # Matches function definitions
            re.MULTILINE
        )
    elif file_path.endswith('.py'):
        # Regex for Python function definitions
        function_regex = re.compile(
            r'^\s*def\s+(\w+)\s*\([^)]*\)\s*:',  # Matches function definitions
            re.MULTILINE
        )
    else:
        return function_definitions  # Skip unsupported file types

    with open(file_path, 'r', encoding='utf-8', errors='ignore') as file:
        content = file.read()
        matches = function_regex.finditer(content)
        for match in matches:
            function_name = match.group(1)
            function_definitions.append(function_name)

    return function_definitions

def setup_function_to_file_mapping(project_path):
    """
    Maps function names to their source files in a project (C/C++ or Python) using regex.
    """
    function_to_file = {}

    # Walk through the project directory
    for root, _, files in os.walk(project_path):
        for file in files:
            file_path = os.path.join(root, file)
            try:
                # Find function definitions in the file
                functions = find_function_definitions(file_path)
                for func in functions:
                    function_to_file[func] = file_path
            except Exception as e:
                print(f"Error processing {file_path}: {e}")

    return function_to_file


def gen_node2func(callgraph_json):
    node2func = {}
    for node in callgraph_json["graph"]["nodes"]:
        node2func[node] = callgraph_json["graph"]["nodes"][node]["name"]
    return node2func


def main():
    import argparse
    parser = argparse.ArgumentParser(description="All call paths from the harness entry to the target function: the "
                                                 "basis of the Realistic context of T1-T5 targets.")
    parser.add_argument("callgraph", help="py: a code2flow JSON (`code2flow --output callgraph.json --language py "
                                          "<project>`); cpp: the text output of LLVM's print-callgraph pass")
    parser.add_argument("src", help="the source node id (py) or function name (cpp)")
    parser.add_argument("dest", help="the destination node id (py) or function name (cpp)")
    parser.add_argument("--language", choices=["py", "cpp"], default="py")
    parser.add_argument("--src_root", help="cpp: the project's source dir, to map functions to files")
    args = parser.parse_args()
    callgraph_path, src, dest = args.callgraph, args.src, args.dest

    if args.language == "cpp":
        graph = parse_call_graph_cpp(callgraph_path)
        # Find all paths
        paths = find_all_paths(graph, src, dest)

        if paths:
            print(f"All paths from '{src}' to '{dest}':")
            for i, path in enumerate(paths, 1):
                print(f"Path {i}: {' -> '.join(path)}")

            if args.src_root:
                print(f"Files containing the functions:")
                func2file = setup_function_to_file_mapping(args.src_root)
                for path in paths:
                    for function in path:
                        if function in func2file:
                            print(f"{function}: {func2file[function]}")
                        else:
                            print(f"{function}: File not found")
        else:
            print(f"No paths found from '{src}' to '{dest}'.")
    else:
        with open(callgraph_path, "r") as f:
            call_graph_json = json.load(f)

        # Initialize the analyzer
        analyzer = PythonCallGraphAnalyzer(call_graph_json)
        node2func = gen_node2func(call_graph_json)

        # Find all paths from src to dest
        paths = analyzer.find_all_paths(src, dest)

        # Print the paths
        for path in paths:
            print(" -> ".join(path))

        print("Func name path:")
        for path in paths:
            for idx, node in enumerate(path):
                if idx != 0:
                    print(" -> ", end="")
                print(node2func[node], end="")
            print()


if __name__ == "__main__":
    main()
