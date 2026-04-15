import os
import sys
import requests
import json

NOTION_TOKEN = os.environ["NOTION_TOKEN"]
NOTION_DATABASE_ID = os.environ["NOTION_DATABASE_ID"]
NOTION_VERSION = "2022-06-28"

def notion_headers():
    return {
        "Authorization": f"Bearer {NOTION_TOKEN}",
        "Notion-Version": NOTION_VERSION,
        "Content-Type": "application/json",
    }

def notion_search_page_by_github_url(github_url: str):
    url = f"https://api.notion.com/v1/databases/{NOTION_DATABASE_ID}/query"
    payload = {
        "filter": {
            "property": "GitHub URL",
            "url": {"equals": github_url}
        }
    }
    r = requests.post(url, headers=notion_headers(), json=payload, timeout=30)
    r.raise_for_status()
    data = r.json()
    results = data.get("results", [])
    return results[0]["id"] if results else None

def notion_create_page(props: dict):
    url = "https://api.notion.com/v1/pages"
    payload = {
        "parent": {"database_id": NOTION_DATABASE_ID},
        "properties": props
    }
    r = requests.post(url, headers=notion_headers(), json=payload, timeout=30)
    r.raise_for_status()
    return r.json()["id"]

def notion_update_page(page_id: str, props: dict):
    url = f"https://api.notion.com/v1/pages/{page_id}"
    payload = {"properties": props}
    r = requests.patch(url, headers=notion_headers(), json=payload, timeout=30)
    r.raise_for_status()
    return r.json()["id"]

def build_properties(issue: dict, repo_full_name: str):
    number = issue["number"]
    title = issue["title"]
    html_url = issue["html_url"]
    state = issue["state"]
    labels = [l["name"] for l in issue.get("labels", [])]

    notion_state = "Open" if state == "open" else "Closed"

    props = {
        "Issue": {"title": [{"text": {"content": f"#{number} {title}"}}]},
        "GitHub URL": {"url": html_url},
        "#": {"number": number},
        "State": {"select": {"name": notion_state}},
        "Repo": {"rich_text": [{"text": {"content": repo_full_name}}]}
    }
    if labels:
        props["Labels"] = {"multi_select": [{"name": name} for name in labels]}
    else:
        props["Labels"] = {"multi_select": []}
    return props

def main():
    event_path = os.environ.get("GITHUB_EVENT_PATH")
    if not event_path or not os.path.exists(event_path):
        print("Kein Event Path gefunden.")
        sys.exit(0)

    with open(event_path, "r", encoding="utf-8") as f:
        event = json.load(f)

    issue = event.get("issue")
    repo = event.get("repository", {})
    repo_full_name = repo.get("full_name", os.environ.get("GH_REPO", ""))

    if not issue:
        print("Kein Issue im Event.")
        return

    github_url = issue["html_url"]
    props = build_properties(issue, repo_full_name)

    try:
        existing_page_id = notion_search_page_by_github_url(github_url)
        if existing_page_id:
            notion_update_page(existing_page_id, props)
            print(f"Update erfolgreich: {github_url}")
        else:
            notion_create_page(props)
            print(f"Erfolgreich neu angelegt: {github_url}")
    except Exception as e:
        print(f"Fehler bei Notion-API: {e}")
        sys.exit(1)

if __name__ == "__main__":
    main()