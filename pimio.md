Wanting to create a photo organization app called pimio. Highlights:

Less about photo touch ups and more about photo organization
Primary emphasis is to create chronlogical photo libraries
Strong emphasis and feature set that aids user to take large sets of photos and organize them chronologically
Tools allow user to apply best-effort timestamps when original metadata is missing or incorrect
  User should be able to select a photo or group of photos and drag and drop it/them within the view to help define its order in the chronlogical timespan
Feature a map view for GPS tagging supporting images and videos
Support basic features like cropping, rotating, creating duplicates, etc. Works for both images and videos
Support clipping large videos into shorter cuts, including automatic scene detection to aid this process
While having the ability to read and write a rich set of metadata types, the app should not overwhelm the user with the details, only provide them if they seek them out
Feature to optionally apply timezone offset to datetime - passing gps location to tzdata or equivalent to ensure correct DST based place and year etc...
Core file store relies on some combination of
  A design similar to what google picasa did with ini files
  Advanced system using LORE version control system (runs both a server and client, abstracted from the user)



Export option that can ensure proper timestamps for either Google Photos or iCloud Photo Library
Hoping to achive a fluid and simple interface akin to Google Picasa
Automatic detection capabilities to detect groups of photos that might be related in time or space, assiting timestamp and location metadata
Support some level of offline facial detection, with the ability to track a face over time from younger to older again with emphasis to aid proper photo timestamping
Support some level of offline landmark detection to aid gps location tagging
Ability to tag videos with multiple GPS coordinates (following some kind of standard?) in the case that the video is moving its location across timeline progression?
