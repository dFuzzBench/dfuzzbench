# -*- coding: utf-8 -*-
"""
Counts the tokens of a project's source code: recursively finds the Python and C/C++
files of a directory, concatenates their content and counts the tokens with the Gemini count_tokens API.

Needs google-generativeai and GEMINI_API_KEY (or GOOGLE_API_KEY) in the environment.

Usage:
   python count_token.py /path/to/your/code/directory [--model MODEL]
"""
import os
import argparse
import google.generativeai as genai

def count_tokens_in_directory(directory_path: str, model_name) -> None:
    """
    Traverses a directory, concatenates specified code files, and counts tokens.

    Args:
        directory_path (str): The absolute or relative path to the directory.
        model_name (str): The name of the model to use for token counting.
    """
    # Supported file extensions for C, C++, and Python
    supported_extensions = {
        ".py",  # Python
        ".c",   # C
        ".h",   # C/C++ Header
        ".cc",  # C++
        ".cpp", # C++
        ".hpp", # C++ Header
        ".cxx", # C++
        ".hxx"  # C++ Header
    }

    if not os.path.isdir(directory_path):
        print(f"Error: Directory not found at '{directory_path}'")
        return

    print(f"Starting token count for directory: {directory_path}")
    print(f"Using model: {model_name}")
    print("="*50)

    full_content = []
    total_files_processed = 0

    # Recursively walk through the directory
    for root, _, files in os.walk(directory_path):
        for file in files:
            # Check if the file has one of the supported extensions
            if any(file.endswith(ext) for ext in supported_extensions):
                file_path = os.path.join(root, file)
                try:
                    # Read the content of the file
                    with open(file_path, 'r', encoding='utf-8', errors='ignore') as f:
                        print(f"Reading file: {file_path}")
                        full_content.append(f.read())
                        total_files_processed += 1
                except IOError as e:
                    print(f"Could not read file {file_path}: {e}")
                except Exception as e:
                    print(f"An unexpected error occurred with file {file_path}: {e}")

    if not full_content:
        print("No supported files found in the directory.")
        return

    # Concatenate all file contents into a single string
    concatenated_content = "\n".join(full_content)
    
    print("\n" + "="*50)
    print(f"Finished processing. Total files read: {total_files_processed}")
    print("Attempting to count tokens with the Gemini API...")

    try:
        genai.configure(api_key=os.environ.get("GEMINI_API_KEY") or os.environ.get("GOOGLE_API_KEY"))
        client = genai.GenerativeModel(model_name=model_name)

        # Count the tokens
        response = client.count_tokens(contents=concatenated_content)
        
        print("\n--- Token Count Result ---")
        print(f"Total Tokens: {response.total_tokens}")
        print("--------------------------")

    except Exception as e:
        print("\n--- Error ---")
        print("An error occurred while communicating with the Google AI API.")
        print(f"Details: {e}")
        print("Please ensure your API key is configured correctly.")
        print("--------------------------")


if __name__ == "__main__":
    # Set up argument parser
    parser = argparse.ArgumentParser(
        description="Count tokens in source code files within a directory."
    )
    parser.add_argument(
        "directory",
        type=str,
        help="The path to the directory containing source code."
    )
    parser.add_argument(
        "--model",
        type=str,
        default="gemini-3.1-pro-preview",
        help="The Gemini model whose tokenizer counts the tokens."
    )

    args = parser.parse_args()

    # Get the absolute path of the directory
    target_directory = os.path.abspath(args.directory)
    
    count_tokens_in_directory(target_directory, args.model)
