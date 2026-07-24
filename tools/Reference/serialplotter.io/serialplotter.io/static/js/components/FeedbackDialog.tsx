import { useState } from "react";
import {
  Dialog,
  DialogContent,
  DialogDescription,
  DialogHeader,
  DialogTitle,
  DialogFooter,
} from "./ui/dialog";
import { Button } from "./ui/button";
import { Textarea } from "./ui/textarea";
import { Star } from "lucide-react";
import { initializeApp, FirebaseApp } from "firebase/app";
import {
  getFirestore,
  collection,
  addDoc,
  serverTimestamp,
  Firestore,
} from "firebase/firestore";
import { toast } from "sonner";

// Initialize Firebase
const firebaseConfig = {
  apiKey: "AIzaSyDsILOSKmO2GY7AwMIeZNjaUNkJhBttFag",
  authDomain: "hb-tec.firebaseapp.com",
  projectId: "hb-tec",
  storageBucket: "hb-tec.appspot.com",
  messagingSenderId: "416914585641",
  appId: "1:416914585641:web:179ca3a8da7047b1be8438",
  measurementId: "G-JRX3Y9QQ7F",
};

// Initialize Firebase and Firestore
let app: FirebaseApp | undefined;
let db: Firestore | undefined;

try {
  app = initializeApp(firebaseConfig, "hb-tec-app");
  db = getFirestore(app);
} catch (error) {
  console.error("Error initializing Firebase:", error);
}

interface FeedbackDialogProps {
  open: boolean;
  onOpenChange: (open: boolean) => void;
}

export function FeedbackDialog({ open, onOpenChange }: FeedbackDialogProps) {
  const [rating, setRating] = useState(0);
  const [comment, setComment] = useState("");
  const [hoverRating, setHoverRating] = useState(0);

  const handleSubmit = async () => {
    if (!db) {
      console.error("Database not initialized");
      return;
    }

    try {
      await addDoc(collection(db, "serialPlotterFeedback"), {
        message: comment,
        noOfStars: rating,
        timeStamp: serverTimestamp(),
      });

      onOpenChange(false);
      setRating(0);
      setComment("");
      toast.success("Feedback sent successfully", { duration: 2000 });
    } catch (error) {
      console.error("Error submitting feedback:", error);
      // You might want to show an error message to the user here
    }
  };

  return (
    <Dialog open={open} onOpenChange={onOpenChange}>
      <DialogContent className="sm:max-w-[425px]">
        <DialogHeader>
          <DialogTitle>Give Feedback</DialogTitle>
          <DialogDescription>
            Rate your experience and let us know how we can improve.
          </DialogDescription>
        </DialogHeader>
        <div className="grid gap-4 py-4">
          <div className="flex items-center justify-center gap-1">
            {[1, 2, 3, 4, 5].map((star) => (
              <button
                key={star}
                onClick={() => setRating(star)}
                onMouseEnter={() => setHoverRating(star)}
                onMouseLeave={() => setHoverRating(0)}
                className={`p-1 transition-colors ${
                  (hoverRating || rating) >= star
                    ? "text-yellow-400"
                    : "text-muted-foreground"
                }`}
                aria-label={`Rate ${star} out of 5`}
                title={`Rate ${star} out of 5`}
              >
                <Star className="h-8 w-8 fill-current" />
              </button>
            ))}
          </div>
          <Textarea
            placeholder="Share your thoughts..."
            value={comment}
            onChange={(e: React.ChangeEvent<HTMLTextAreaElement>) =>
              setComment(e.target.value)
            }
            className="min-h-[100px]"
          />
        </div>
        <DialogFooter>
          <Button onClick={handleSubmit} disabled={rating === 0}>
            Submit Feedback
          </Button>
        </DialogFooter>
      </DialogContent>
    </Dialog>
  );
}
