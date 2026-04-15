import os
import sys
import requests

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
    state = issue["state"]  # "open" / "closed"
    labels = [l["name"] for l in issue.get("labels", [])]

    # Map GitHub state to your Notion select options ("Open" / "Closed")
    notion_state = "Open" if state == "open" else "Closed"

    # IMPORTANT: Property names must match your Notion DB exactly:
    # "Issue", "GitHub URL", "#", "State", "Labels", "Repo"
    props = {
        "Issue": {
            "title": [{"text": {"content": f"#{number} {title}"}}]
        },
        "GitHub URL": {
            "url": html_url
        },
        "#": {
            "number": number
        },
        "State": {
            "select": {"name": notion_state}
        },
        "Repo": {
            "rich_text": [{"text": {"content": repo_full_name}}]
        }
    }

    # Only send Labels if present (avoids odd behavior on empty)
    if labels:
        props["Labels"] = {"multi_select": [{"name": name} for name in labels]}
    else:
        props["Labels"] = {"multi_select": []}

    return props

def main():
    event_path = os.environ.get("GITHUB_EVENT_PATH")
    if not event_path:
        print("GITHUB_EVENT_PATH not set", file=sys.stderr)
        sys.exit(1)

    with open(event_path, "r", encoding="utf-8") as f:
        event = __import__("json").load(f)

    issue = event.get("issue")
    repo = event.get("repository", {})
    repo_full_name = repo.get("full_name", os.environ.get("GH_REPO", ""))

    if not issue:
        print("No issue in event payload. Exiting.")
        return

    github_url = issue["html_url"]
    props = build_properties(issue, repo_full_name)

    existing_page_id = notion_search_page_by_github_url(github_url)
    if existing_page_id:
        notion_update_page(existing_page_id, props)
        print(f"Updated Notion page for {github_url}")
    else:
        notion_create_page(props)
        print(f"Created Notion page for {github_url}")

if __name__ == "__main__":
    main()