import {
  Dialog,
  DialogContent,
  DialogHeader,
  DialogTitle,
} from "./ui/dialog"
import { Input } from "./ui/input"
import { Button } from "./ui/button"
import { useState, useEffect } from "react"

export function SingleChannelDialog({
  open,
  onOpenChange,
  channel,
  currentName,
  onChannelRename,
}: {
  open: boolean
  onOpenChange: (open: boolean) => void
  channel: string
  currentName: string
  onChannelRename: (oldName: string, newName: string) => void
}) {
  const [newName, setNewName] = useState(currentName)

  useEffect(() => {
    if (open) {
      setNewName(currentName)
    }
  }, [open, currentName])

  const handleSave = () => {
    if (newName && newName !== channel) {
      onChannelRename(channel, newName.trim())
    }
    onOpenChange(false)
  }

  return (
    <Dialog open={open} onOpenChange={onOpenChange}>
      <DialogContent className="sm:max-w-[425px]">
        <DialogHeader>
          <DialogTitle>Rename Channel</DialogTitle>
        </DialogHeader>
        <div className="grid gap-4 py-4">
          <div className="grid grid-cols-4 items-center gap-4">
            <Input
              className="col-span-4"
              placeholder="Enter new name"
              value={newName}
              onChange={(e) => setNewName(e.target.value)}
              onKeyDown={(e) => {
                if (e.key === 'Enter') {
                  handleSave()
                }
              }}
            />
          </div>
        </div>
        <div className="flex justify-end gap-4">
          <Button variant="outline" onClick={() => onOpenChange(false)}>Cancel</Button>
          <Button onClick={handleSave}>Save</Button>
        </div>
      </DialogContent>
    </Dialog>
  )
}